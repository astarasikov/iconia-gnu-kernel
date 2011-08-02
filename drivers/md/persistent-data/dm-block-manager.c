/*
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */
#include "dm-block-manager.h"
#include "dm-persistent-data-internal.h"

#include <linux/dm-io.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "block manager"

/*----------------------------------------------------------------*/

#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#define MAX_CACHE_SIZE 16U

enum dm_block_state {
	BS_EMPTY,
	BS_CLEAN,
	BS_READING,
	BS_WRITING,
	BS_READ_LOCKED,
	BS_READ_LOCKED_DIRTY,	/* Block was dirty before it was read locked. */
	BS_WRITE_LOCKED,
	BS_DIRTY,
	BS_ERROR
};

struct dm_block {
	struct list_head list;
	struct hlist_node hlist;

	dm_block_t where;
	struct dm_block_validator *validator;
	void *data;
	wait_queue_head_t io_q;
	unsigned read_lock_count;
	unsigned write_lock_pending;
	enum dm_block_state state;

	/*
	 * Extra flags like REQ_FLUSH and REQ_FUA can be set here.  This is
	 * mainly as to avoid a race condition in flush_and_unlock() where
	 * the newly-unlocked superblock may have been submitted for a
	 * write before the write_all_dirty() call is made.
	 */
	int io_flags;

	/*
	 * Sadly we need an up pointer so we can get to the bm on io
	 * completion.
	 */
	struct dm_block_manager *bm;
};

struct dm_block_manager {
	struct block_device *bdev;
	unsigned cache_size;	/* In bytes */
	unsigned block_size;	/* In bytes */
	dm_block_t nr_blocks;

	/*
	 * This will trigger every time an io completes.
	 */
	wait_queue_head_t io_q;

	struct dm_io_client *io;

	/*
	 * Protects all the lists and the hash table.
	 */
	spinlock_t lock;

	unsigned available_count;
	unsigned reading_count;
	unsigned writing_count;

	struct list_head empty_list;	/* No block assigned */
	struct list_head clean_list;	/* Unlocked and clean */
	struct list_head dirty_list;	/* Unlocked and dirty */
	struct list_head error_list;

	char buffer_cache_name[32];
	struct kmem_cache *buffer_cache; /* The buffers that store the raw data */

	/*
	 * Hash table of cached blocks, holds everything that isn't in the
	 * BS_EMPTY state.
	 */
	unsigned hash_size;
	unsigned hash_mask;

	struct hlist_head buckets[0];	/* Must be last member of struct. */
};

dm_block_t dm_block_location(struct dm_block *b)
{
	return b->where;
}
EXPORT_SYMBOL_GPL(dm_block_location);

void *dm_block_data(struct dm_block *b)
{
	return b->data;
}
EXPORT_SYMBOL_GPL(dm_block_data);

/*----------------------------------------------------------------
 * Hash table
 *--------------------------------------------------------------*/
static struct dm_block *__find_block(struct dm_block_manager *bm, dm_block_t b)
{
	unsigned bucket = dm_hash_block(b, bm->hash_mask);
	struct dm_block *blk;
	struct hlist_node *n;

	hlist_for_each_entry(blk, n, bm->buckets + bucket, hlist)
		if (blk->where == b)
			return blk;

	return NULL;
}

static void __insert_block(struct dm_block_manager *bm, struct dm_block *b)
{
	unsigned bucket = dm_hash_block(b->where, bm->hash_mask);

	hlist_add_head(&b->hlist, bm->buckets + bucket);
}

/*----------------------------------------------------------------
 * Block state:
 * __transition() handles transition of a block between different states.
 * Study this to understand the state machine.
 *
 * Alternatively install graphviz and run:
 *     grep DOT dm-block-manager.c | grep -v '	' |
 *	 sed -e 's/.*DOT: //' -e 's/\*\///' |
 *	 dot -Tps -o states.ps
 *
 * Assumes bm->lock is held.
 *--------------------------------------------------------------*/
static void __transition(struct dm_block *b, enum dm_block_state new_state)
{
	/* DOT: digraph BlockStates { */
	struct dm_block_manager *bm = b->bm;

	switch (new_state) {
	case BS_EMPTY:
		/* DOT: error -> empty */
		/* DOT: clean -> empty */
		BUG_ON(!((b->state == BS_ERROR) ||
			 (b->state == BS_CLEAN)));
		hlist_del(&b->hlist);
		list_move(&b->list, &bm->empty_list);
		b->write_lock_pending = 0;
		b->read_lock_count = 0;
		b->io_flags = 0;
		b->validator = NULL;

		if (b->state == BS_ERROR)
			bm->available_count++;
		break;

	case BS_CLEAN:
		/* DOT: reading -> clean */
		/* DOT: writing -> clean */
		/* DOT: read_locked -> clean */
		BUG_ON(!((b->state == BS_READING) ||
			 (b->state == BS_WRITING) ||
			 (b->state == BS_READ_LOCKED)));
		switch (b->state) {
		case BS_READING:
			BUG_ON(!bm->reading_count);
			bm->reading_count--;
			break;

		case BS_WRITING:
			BUG_ON(!bm->writing_count);
			bm->writing_count--;
			b->io_flags = 0;
			break;

		default:
			break;
		}
		list_add_tail(&b->list, &bm->clean_list);
		bm->available_count++;
		break;

	case BS_READING:
		/* DOT: empty -> reading */
		BUG_ON(!(b->state == BS_EMPTY));
		__insert_block(bm, b);
		list_del(&b->list);
		bm->available_count--;
		bm->reading_count++;
		break;

	case BS_WRITING:
		/* DOT: dirty -> writing */
		BUG_ON(!(b->state == BS_DIRTY));
		list_del(&b->list);
		bm->writing_count++;
		break;

	case BS_READ_LOCKED:
		/* DOT: clean -> read_locked */
		BUG_ON(!(b->state == BS_CLEAN));
		list_del(&b->list);
		bm->available_count--;
		break;

	case BS_READ_LOCKED_DIRTY:
		/* DOT: dirty -> read_locked_dirty */
		BUG_ON(!((b->state == BS_DIRTY)));
		list_del(&b->list);
		break;

	case BS_WRITE_LOCKED:
		/* DOT: dirty -> write_locked */
		/* DOT: clean -> write_locked */
		BUG_ON(!((b->state == BS_DIRTY) ||
			 (b->state == BS_CLEAN)));
		list_del(&b->list);

		if (b->state == BS_CLEAN)
			bm->available_count--;
		break;

	case BS_DIRTY:
		/* DOT: write_locked -> dirty */
		/* DOT: read_locked_dirty -> dirty */
		BUG_ON(!((b->state == BS_WRITE_LOCKED) ||
			 (b->state == BS_READ_LOCKED_DIRTY)));
		list_add_tail(&b->list, &bm->dirty_list);
		break;

	case BS_ERROR:
		/* DOT: writing -> error */
		/* DOT: reading -> error */
		BUG_ON(!((b->state == BS_WRITING) ||
			 (b->state == BS_READING)));
		list_add_tail(&b->list, &bm->error_list);
		break;
	}

	b->state = new_state;
	/* DOT: } */
}

/*----------------------------------------------------------------
 * Low-level io.
 *--------------------------------------------------------------*/
typedef void (completion_fn)(unsigned long error, struct dm_block *b);

static void submit_io(struct dm_block *b, int rw,
		      completion_fn fn)
{
	struct dm_block_manager *bm = b->bm;
	struct dm_io_request req;
	struct dm_io_region region;
	unsigned sectors_per_block = bm->block_size >> SECTOR_SHIFT;

	region.bdev = bm->bdev;
	region.sector = b->where * sectors_per_block;
	region.count = sectors_per_block;

	req.bi_rw = rw;
	req.mem.type = DM_IO_KMEM;
	req.mem.offset = 0;
	req.mem.ptr.addr = b->data;
	req.notify.fn = (void (*)(unsigned long, void *)) fn;
	req.notify.context = b;
	req.client = bm->io;

	if (dm_io(&req, 1, &region, NULL) < 0)
		fn(1, b);
}

/*----------------------------------------------------------------
 * High-level io.
 *--------------------------------------------------------------*/
static void __complete_io(unsigned long error, struct dm_block *b)
{
	struct dm_block_manager *bm = b->bm;

	if (error) {
		DMERR("io error = %lu, block = %llu",
		      error , (unsigned long long)b->where);
		__transition(b, BS_ERROR);
	} else
		__transition(b, BS_CLEAN);

	wake_up(&b->io_q);
	wake_up(&bm->io_q);
}

static void complete_io(unsigned long error, struct dm_block *b)
{
	struct dm_block_manager *bm = b->bm;
	unsigned long flags;

	spin_lock_irqsave(&bm->lock, flags);
	__complete_io(error, b);
	spin_unlock_irqrestore(&bm->lock, flags);
}

static void read_block(struct dm_block *b)
{
	submit_io(b, READ, complete_io);
}

static void write_block(struct dm_block *b)
{
	if (b->validator)
		b->validator->prepare_for_write(b->validator, b,
						b->bm->block_size);

	submit_io(b, WRITE | b->io_flags, complete_io);
}

static void write_dirty(struct dm_block_manager *bm, unsigned count)
{
	struct dm_block *b, *tmp;
	struct list_head dirty;
	unsigned long flags;

	/*
	 * Grab the first @count entries from the dirty list
	 */
	INIT_LIST_HEAD(&dirty);
	spin_lock_irqsave(&bm->lock, flags);
	list_for_each_entry_safe(b, tmp, &bm->dirty_list, list) {
		if (!count--)
			break;
		__transition(b, BS_WRITING);
		list_add_tail(&b->list, &dirty);
	}
	spin_unlock_irqrestore(&bm->lock, flags);

	list_for_each_entry_safe(b, tmp, &dirty, list) {
		list_del(&b->list);
		write_block(b);
	}
}

static void write_all_dirty(struct dm_block_manager *bm)
{
	write_dirty(bm, bm->cache_size);
}

static void __clear_errors(struct dm_block_manager *bm)
{
	struct dm_block *b, *tmp;
	list_for_each_entry_safe(b, tmp, &bm->error_list, list)
		__transition(b, BS_EMPTY);
}

/*----------------------------------------------------------------
 * Waiting
 *--------------------------------------------------------------*/
#ifdef __CHECKER__
#  define __retains(x)	__attribute__((context(x, 1, 1)))
#else
#  define __retains(x)
#endif

#define __wait_block(wq, lock, flags, sched_fn, condition)	\
do {								\
	int r = 0;						\
								\
	DEFINE_WAIT(wait);					\
	add_wait_queue(wq, &wait);				\
								\
	for (;;) {						\
		prepare_to_wait(wq, &wait, TASK_INTERRUPTIBLE); \
		if (condition)					\
			break;					\
								\
		spin_unlock_irqrestore(lock, flags);		\
		if (signal_pending(current)) {			\
			r = -ERESTARTSYS;			\
			spin_lock_irqsave(lock, flags);		\
			break;					\
		}						\
								\
		sched_fn();					\
		spin_lock_irqsave(lock, flags);			\
	}							\
								\
	finish_wait(wq, &wait);					\
	return r;						\
} while (0)

static int __wait_io(struct dm_block *b, unsigned long *flags)
	__retains(&b->bm->lock)
{
	__wait_block(&b->io_q, &b->bm->lock, *flags, io_schedule,
		     ((b->state != BS_READING) && (b->state != BS_WRITING)));
}

static int __wait_unlocked(struct dm_block *b, unsigned long *flags)
	__retains(&b->bm->lock)
{
	__wait_block(&b->io_q, &b->bm->lock, *flags, schedule,
		     ((b->state == BS_CLEAN) || (b->state == BS_DIRTY)));
}

static int __wait_read_lockable(struct dm_block *b, unsigned long *flags)
	__retains(&b->bm->lock)
{
	__wait_block(&b->io_q, &b->bm->lock, *flags, schedule,
		     (!b->write_lock_pending && (b->state == BS_CLEAN ||
						 b->state == BS_DIRTY ||
						 b->state == BS_READ_LOCKED)));
}

static int __wait_all_writes(struct dm_block_manager *bm, unsigned long *flags)
	__retains(&bm->lock)
{
	__wait_block(&bm->io_q, &bm->lock, *flags, io_schedule,
		     !bm->writing_count);
}

static int __wait_all_io(struct dm_block_manager *bm, unsigned long *flags)
	__retains(&bm->lock)
{
	__wait_block(&bm->io_q, &bm->lock, *flags, io_schedule,
		     !bm->writing_count && !bm->reading_count);
}

static int __wait_clean(struct dm_block_manager *bm, unsigned long *flags)
	__retains(&bm->lock)
{
	__wait_block(&bm->io_q, &bm->lock, *flags, io_schedule,
		     (!list_empty(&bm->clean_list) ||
		      (!bm->writing_count)));
}

/*----------------------------------------------------------------
 * Finding a free block to recycle
 *--------------------------------------------------------------*/
static int recycle_block(struct dm_block_manager *bm, dm_block_t where,
			 int need_read, struct dm_block_validator *v,
			 struct dm_block **result)
{
	int r = 0;
	struct dm_block *b;
	unsigned long flags, available;

	/*
	 * Wait for a block to appear on the empty or clean lists.
	 */
	spin_lock_irqsave(&bm->lock, flags);
	while (1) {
		/*
		 * Once we can lock and do io concurrently then we should
		 * probably flush at bm->cache_size / 2 and write _all_
		 * dirty blocks.
		 */
		available = bm->available_count + bm->writing_count;
		if (available < bm->cache_size / 4) {
			spin_unlock_irqrestore(&bm->lock, flags);
			write_dirty(bm, bm->cache_size / 4);
			spin_lock_irqsave(&bm->lock, flags);
		}

		if (!list_empty(&bm->empty_list)) {
			b = list_first_entry(&bm->empty_list, struct dm_block, list);
			break;

		} else if (!list_empty(&bm->clean_list)) {
			b = list_first_entry(&bm->clean_list, struct dm_block, list);
			__transition(b, BS_EMPTY);
			break;
		}

		__wait_clean(bm, &flags);
	}

	b->where = where;
	b->validator = v;
	__transition(b, BS_READING);

	if (!need_read) {
		memset(b->data, 0, bm->block_size);
		__transition(b, BS_CLEAN);
	} else {
		spin_unlock_irqrestore(&bm->lock, flags);
		read_block(b);
		spin_lock_irqsave(&bm->lock, flags);
		__wait_io(b, &flags);

		/* FIXME: Can b have been recycled between io completion and here? */

		/*
		 * Did the io succeed?
		 */
		if (b->state == BS_ERROR) {
			/*
			 * Since this is a read that has failed we can clear the error
			 * immediately.	 Failed writes are revealed during a commit.
			 */
			__transition(b, BS_EMPTY);
			r = -EIO;
		}

		if (b->validator) {
			r = b->validator->check(b->validator, b, bm->block_size);
			if (r) {
				DMERR("%s validator check failed for block %llu",
				      b->validator->name, (unsigned long long)b->where);
				__transition(b, BS_EMPTY);
			}
		}
	}
	spin_unlock_irqrestore(&bm->lock, flags);

	if (!r)
		*result = b;

	return r;
}

/*----------------------------------------------------------------
 * Low level block management
 *--------------------------------------------------------------*/

static struct kmem_cache *dm_block_cache;  /* struct dm_block */

static struct dm_block *alloc_block(struct dm_block_manager *bm)
{
	struct dm_block *b = kmem_cache_alloc(dm_block_cache, GFP_KERNEL);

	if (!b)
		return NULL;

	INIT_LIST_HEAD(&b->list);
	INIT_HLIST_NODE(&b->hlist);

	b->data = kmem_cache_alloc(bm->buffer_cache, GFP_KERNEL);
	if (!b->data) {
		kmem_cache_free(dm_block_cache, b);
		return NULL;
	}

	b->validator = NULL;
	b->state = BS_EMPTY;
	init_waitqueue_head(&b->io_q);
	b->read_lock_count = 0;
	b->write_lock_pending = 0;
	b->io_flags = 0;
	b->bm = bm;

	return b;
}

static void free_block(struct dm_block *b)
{
	kmem_cache_free(b->bm->buffer_cache, b->data);
	kmem_cache_free(dm_block_cache, b);
}

static int populate_bm(struct dm_block_manager *bm, unsigned count)
{
	int i;
	LIST_HEAD(bs);

	for (i = 0; i < count; i++) {
		struct dm_block *b = alloc_block(bm);
		if (!b) {
			struct dm_block *tmp;
			list_for_each_entry_safe(b, tmp, &bs, list)
				free_block(b);
			return -ENOMEM;
		}

		list_add(&b->list, &bs);
	}

	list_replace(&bs, &bm->empty_list);
	bm->available_count = count;

	return 0;
}

/*----------------------------------------------------------------
 * Public interface
 *--------------------------------------------------------------*/
static unsigned calc_hash_size(unsigned cache_size)
{
	unsigned r = 32;	/* Minimum size is 16 */

	while (r < cache_size)
		r <<= 1;

	return r >> 1;
}

struct dm_block_manager *dm_block_manager_create(struct block_device *bdev,
						 unsigned block_size,
						 unsigned cache_size)
{
	unsigned i;
	unsigned hash_size = calc_hash_size(cache_size);
	size_t len = sizeof(struct dm_block_manager) +
		     sizeof(struct hlist_head) * hash_size;
	struct dm_block_manager *bm;

	bm = kmalloc(len, GFP_KERNEL);
	if (!bm)
		return NULL;

	bm->bdev = bdev;
	bm->cache_size = max(MAX_CACHE_SIZE, cache_size);
	bm->block_size = block_size;
	bm->nr_blocks = i_size_read(bdev->bd_inode);
	do_div(bm->nr_blocks, block_size);
	init_waitqueue_head(&bm->io_q);
	spin_lock_init(&bm->lock);

	INIT_LIST_HEAD(&bm->empty_list);
	INIT_LIST_HEAD(&bm->clean_list);
	INIT_LIST_HEAD(&bm->dirty_list);
	INIT_LIST_HEAD(&bm->error_list);
	bm->available_count = 0;
	bm->reading_count = 0;
	bm->writing_count = 0;

	sprintf(bm->buffer_cache_name, "dm_block_buffer-%d-%d",
		MAJOR(disk_devt(bdev->bd_disk)),
		MINOR(disk_devt(bdev->bd_disk)));

	bm->buffer_cache = kmem_cache_create(bm->buffer_cache_name,
					     block_size, SECTOR_SIZE,
					     0, NULL);
	if (!bm->buffer_cache)
		goto bad_bm;

	bm->hash_size = hash_size;
	bm->hash_mask = hash_size - 1;
	for (i = 0; i < hash_size; i++)
		INIT_HLIST_HEAD(bm->buckets + i);

	bm->io = dm_io_client_create();
	if (!bm->io)
		goto bad_buffer_cache;

	if (populate_bm(bm, cache_size) < 0)
		goto bad_io_client;

	return bm;

bad_io_client:
	dm_io_client_destroy(bm->io);
bad_buffer_cache:
	kmem_cache_destroy(bm->buffer_cache);
bad_bm:
	kfree(bm);

	return NULL;
}
EXPORT_SYMBOL_GPL(dm_block_manager_create);

void dm_block_manager_destroy(struct dm_block_manager *bm)
{
	int i;
	struct dm_block *b, *btmp;
	struct hlist_node *n, *tmp;

	dm_io_client_destroy(bm->io);

	for (i = 0; i < bm->hash_size; i++)
		hlist_for_each_entry_safe(b, n, tmp, bm->buckets + i, hlist)
			free_block(b);

	list_for_each_entry_safe(b, btmp, &bm->empty_list, list)
		free_block(b);

	kmem_cache_destroy(bm->buffer_cache);

	kfree(bm);
}
EXPORT_SYMBOL_GPL(dm_block_manager_destroy);

unsigned dm_bm_block_size(struct dm_block_manager *bm)
{
	return bm->block_size;
}
EXPORT_SYMBOL_GPL(dm_bm_block_size);

dm_block_t dm_bm_nr_blocks(struct dm_block_manager *bm)
{
	return bm->nr_blocks;
}

static int lock_internal(struct dm_block_manager *bm, dm_block_t block,
			 int how, int need_read, int can_block,
			 struct dm_block_validator *v,
			 struct dm_block **result)
{
	int r = 0;
	struct dm_block *b;
	unsigned long flags;

	spin_lock_irqsave(&bm->lock, flags);
retry:
	b = __find_block(bm, block);
	if (b) {
		if (!need_read)
			b->validator = v;
		else {
			if (b->validator && (v != b->validator)) {
				DMERR("validator mismatch (old=%s vs new=%s) for block %llu",
				      b->validator->name, v ? v->name : "NULL",
				      (unsigned long long)b->where);
				spin_unlock_irqrestore(&bm->lock, flags);
				return -EINVAL;

			}
			if (!b->validator && v) {
				b->validator = v;
				r = b->validator->check(b->validator, b, bm->block_size);
				if (r) {
					DMERR("%s validator check failed for block %llu",
					      b->validator->name,
					      (unsigned long long)b->where);
					spin_unlock_irqrestore(&bm->lock, flags);
					return r;
				}
			}
		}

		switch (how) {
		case READ:
			if (b->write_lock_pending || (b->state != BS_CLEAN &&
						      b->state != BS_DIRTY &&
						      b->state != BS_READ_LOCKED)) {
				if (!can_block) {
					spin_unlock_irqrestore(&bm->lock, flags);
					return -EWOULDBLOCK;
				}

				__wait_read_lockable(b, &flags);

				if (b->where != block)
					goto retry;
			}
			break;

		case WRITE:
			while (b->state != BS_CLEAN && b->state != BS_DIRTY) {
				if (!can_block) {
					spin_unlock_irqrestore(&bm->lock, flags);
					return -EWOULDBLOCK;
				}

				b->write_lock_pending++;
				__wait_unlocked(b, &flags);
				b->write_lock_pending--;
				if (b->where != block)
					goto retry;
			}
			break;
		}

	} else if (!can_block) {
		r = -EWOULDBLOCK;
		goto out;

	} else {
		spin_unlock_irqrestore(&bm->lock, flags);
		r = recycle_block(bm, block, need_read, v, &b);
		spin_lock_irqsave(&bm->lock, flags);
	}

	if (!r) {
		switch (how) {
		case READ:
			b->read_lock_count++;

			if (b->state == BS_DIRTY)
				__transition(b, BS_READ_LOCKED_DIRTY);
			else if (b->state == BS_CLEAN)
				__transition(b, BS_READ_LOCKED);
			break;

		case WRITE:
			__transition(b, BS_WRITE_LOCKED);
			break;
		}

		*result = b;
	}

out:
	spin_unlock_irqrestore(&bm->lock, flags);

	return r;
}

int dm_bm_read_lock(struct dm_block_manager *bm, dm_block_t b,
		    struct dm_block_validator *v,
		    struct dm_block **result)
{
	return lock_internal(bm, b, READ, 1, 1, v, result);
}
EXPORT_SYMBOL_GPL(dm_bm_read_lock);

int dm_bm_write_lock(struct dm_block_manager *bm,
		     dm_block_t b, struct dm_block_validator *v,
		     struct dm_block **result)
{
	return lock_internal(bm, b, WRITE, 1, 1, v, result);
}
EXPORT_SYMBOL_GPL(dm_bm_write_lock);

int dm_bm_read_try_lock(struct dm_block_manager *bm,
			dm_block_t b, struct dm_block_validator *v,
			struct dm_block **result)
{
	return lock_internal(bm, b, READ, 1, 0, v, result);
}

int dm_bm_write_lock_zero(struct dm_block_manager *bm,
			  dm_block_t b, struct dm_block_validator *v,
			  struct dm_block **result)
{
	int r = lock_internal(bm, b, WRITE, 0, 1, v, result);

	if (!r)
		memset((*result)->data, 0, bm->block_size);

	return r;
}

int dm_bm_unlock(struct dm_block *b)
{
	int r = 0;
	unsigned long flags;

	spin_lock_irqsave(&b->bm->lock, flags);
	switch (b->state) {
	case BS_WRITE_LOCKED:
		__transition(b, BS_DIRTY);
		wake_up(&b->io_q);
		break;

	case BS_READ_LOCKED:
		if (!--b->read_lock_count) {
			__transition(b, BS_CLEAN);
			wake_up(&b->io_q);
		}
		break;

	case BS_READ_LOCKED_DIRTY:
		if (!--b->read_lock_count) {
			__transition(b, BS_DIRTY);
			wake_up(&b->io_q);
		}
		break;

	default:
		DMERR("block = %llu not locked",
		      (unsigned long long)b->where);
		r = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&b->bm->lock, flags);

	return r;
}
EXPORT_SYMBOL_GPL(dm_bm_unlock);

static int __wait_flush(struct dm_block_manager *bm)
{
	int r = 0;
	unsigned long flags;

	spin_lock_irqsave(&bm->lock, flags);
	__wait_all_writes(bm, &flags);

	if (!list_empty(&bm->error_list)) {
		r = -EIO;
		__clear_errors(bm);
	}
	spin_unlock_irqrestore(&bm->lock, flags);

	return r;
}

int dm_bm_flush_and_unlock(struct dm_block_manager *bm,
			   struct dm_block *superblock)
{
	int r;
	unsigned long flags;

	write_all_dirty(bm);
	r = __wait_flush(bm);
	if (r)
		return r;

	spin_lock_irqsave(&bm->lock, flags);
	superblock->io_flags = REQ_FUA | REQ_FLUSH;
	spin_unlock_irqrestore(&bm->lock, flags);

	dm_bm_unlock(superblock);
	write_all_dirty(bm);

	return __wait_flush(bm);
}

int dm_bm_rebind_block_device(struct dm_block_manager *bm,
			      struct block_device *bdev)
{
	unsigned long flags;
	dm_block_t nr_blocks = i_size_read(bdev->bd_inode);

	do_div(nr_blocks, bm->block_size);

	spin_lock_irqsave(&bm->lock, flags);
	if (nr_blocks < bm->nr_blocks) {
		spin_unlock_irqrestore(&bm->lock, flags);
		return -EINVAL;
	}

	bm->bdev = bdev;
	bm->nr_blocks = nr_blocks;

	/*
	 * Wait for any in-flight io that may be using the old bdev
	 */
	__wait_all_io(bm, &flags);
	spin_unlock_irqrestore(&bm->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bm_rebind_block_device);

/*----------------------------------------------------------------*/

static int __init init_persistent_data(void)
{
	dm_block_cache = KMEM_CACHE(dm_block, SLAB_HWCACHE_ALIGN);
	if (!dm_block_cache)
		return -ENOMEM;

	return 0;
}

static void __exit exit_persistent_data(void)
{
	kmem_cache_destroy(dm_block_cache);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_DESCRIPTION("Immutable metadata library for dm");
module_init(init_persistent_data);
module_exit(exit_persistent_data);
