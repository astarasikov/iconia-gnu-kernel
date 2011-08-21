/*
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-space-map-common.h"
#include "dm-space-map-disk.h"
#include "dm-space-map.h"
#include "dm-transaction-manager.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "space map disk"

/*
 * Bitmap validator
 */
static void bitmap_prepare_for_write(struct dm_block_validator *v,
				     struct dm_block *b,
				     size_t block_size)
{
	struct disk_bitmap_header *disk_header = dm_block_data(b);

	disk_header->blocknr = cpu_to_le64(dm_block_location(b));
	disk_header->csum = cpu_to_le32(dm_block_csum_data(&disk_header->not_used, block_size - sizeof(__le32)));
}

static int bitmap_check(struct dm_block_validator *v,
			struct dm_block *b,
			size_t block_size)
{
	struct disk_bitmap_header *disk_header = dm_block_data(b);
	__le32 csum_disk;

	if (dm_block_location(b) != le64_to_cpu(disk_header->blocknr)) {
		DMERR("bitmap check failed blocknr %llu wanted %llu",
		      le64_to_cpu(disk_header->blocknr), dm_block_location(b));
		return -ENOTBLK;
	}

	csum_disk = cpu_to_le32(dm_block_csum_data(&disk_header->not_used, block_size - sizeof(__le32)));
	if (csum_disk != disk_header->csum) {
		DMERR("bitmap check failed csum %u wanted %u",
		      le32_to_cpu(csum_disk), le32_to_cpu(disk_header->csum));
		return -EILSEQ;
	}

	return 0;
}

struct dm_block_validator dm_sm_bitmap_validator = {
	.name = "sm_bitmap",
	.prepare_for_write = bitmap_prepare_for_write,
	.check = bitmap_check
};

/*----------------------------------------------------------------*/

#define ENTRIES_PER_WORD 32
#define ENTRIES_SHIFT	5

void *dm_bitmap_data(struct dm_block *b)
{
	return dm_block_data(b) + sizeof(struct disk_bitmap_header);
}

#define WORD_MASK_LOW 0x5555555555555555ULL
#define WORD_MASK_HIGH 0xAAAAAAAAAAAAAAAAULL
#define WORD_MASK_ALL 0xFFFFFFFFFFFFFFFFULL

static unsigned bitmap_word_used(void *addr, unsigned b)
{
	__le64 *words_le = addr;
	__le64 *w_le = words_le + (b >> ENTRIES_SHIFT);

	uint64_t bits = le64_to_cpu(*w_le);

	return ((bits & WORD_MASK_LOW) == WORD_MASK_LOW ||
		(bits & WORD_MASK_HIGH) == WORD_MASK_HIGH ||
		(bits & WORD_MASK_ALL) == WORD_MASK_ALL);
}

unsigned sm_lookup_bitmap(void *addr, unsigned b)
{
	__le64 *words_le = addr;
	__le64 *w_le = words_le + (b >> ENTRIES_SHIFT);

	b = (b & (ENTRIES_PER_WORD - 1)) << 1;

	return (!!test_bit_le(b, (void *) w_le) << 1) |
		(!!test_bit_le(b + 1, (void *) w_le));
}

void sm_set_bitmap(void *addr, unsigned b, unsigned val)
{
	__le64 *words_le = addr;
	__le64 *w_le = words_le + (b >> ENTRIES_SHIFT);

	b = (b & (ENTRIES_PER_WORD - 1)) << 1;

	if (val & 2)
		__set_bit_le(b, (void *) w_le);
	else
		__clear_bit_le(b, (void *) w_le);

	if (val & 1)
		__set_bit_le(b + 1, (void *) w_le);
	else
		__clear_bit_le(b + 1, (void *) w_le);
}

int sm_find_free(void *addr, unsigned begin, unsigned end,
		 unsigned *result)
{
	while (begin < end) {
		if (!(begin & (ENTRIES_PER_WORD - 1)) &&
		    bitmap_word_used(addr, begin)) {
			begin += ENTRIES_PER_WORD;
			continue;
		}

		if (!sm_lookup_bitmap(addr, begin)) {
			*result = begin;
			return 0;
		}

		begin++;
	}

	return -ENOSPC;
}

static int disk_ll_init(struct ll_disk *io, struct dm_transaction_manager *tm)
{
	io->tm = tm;
	io->bitmap_info.tm = tm;
	io->bitmap_info.levels = 1;

	/*
	 * Because the new bitmap blocks are created via a shadow
	 * operation, the old entry has already had its reference count
	 * decremented and we don't need the btree to do any bookkeeping.
	 */
	io->bitmap_info.value_type.size = sizeof(struct disk_index_entry);
	io->bitmap_info.value_type.inc = NULL;
	io->bitmap_info.value_type.dec = NULL;
	io->bitmap_info.value_type.equal = NULL;

	io->ref_count_info.tm = tm;
	io->ref_count_info.levels = 1;
	io->ref_count_info.value_type.size = sizeof(uint32_t);
	io->ref_count_info.value_type.inc = NULL;
	io->ref_count_info.value_type.dec = NULL;
	io->ref_count_info.value_type.equal = NULL;

	io->block_size = dm_bm_block_size(dm_tm_get_bm(tm));

	if (io->block_size > (1 << 30)) {
		DMERR("block size too big to hold bitmaps");
		return -EINVAL;
	}

	io->entries_per_block = (io->block_size - sizeof(struct disk_bitmap_header)) *
				ENTRIES_PER_BYTE;
	io->nr_blocks = 0;
	io->bitmap_root = 0;
	io->ref_count_root = 0;

	return 0;
}

static int disk_ll_new(struct ll_disk *io, struct dm_transaction_manager *tm)
{
	int r;

	r = disk_ll_init(io, tm);
	if (r < 0)
		return r;

	io->nr_blocks = 0;
	io->nr_allocated = 0;
	r = dm_btree_empty(&io->bitmap_info, &io->bitmap_root);
	if (r < 0)
		return r;

	r = dm_btree_empty(&io->ref_count_info, &io->ref_count_root);
	if (r < 0) {
		dm_btree_del(&io->bitmap_info, io->bitmap_root);
		return r;
	}

	return 0;
}

static int disk_ll_extend(struct ll_disk *io, dm_block_t extra_blocks)
{
	int r;
	dm_block_t i, nr_blocks;
	unsigned old_blocks, blocks;

	nr_blocks = io->nr_blocks + extra_blocks;
	old_blocks = dm_sector_div_up(io->nr_blocks, io->entries_per_block);
	blocks = dm_sector_div_up(nr_blocks, io->entries_per_block);

	for (i = old_blocks; i < blocks; i++) {
		struct dm_block *b;
		struct disk_index_entry idx;

		r = dm_tm_new_block(io->tm, &dm_sm_bitmap_validator, &b);
		if (r < 0)
			return r;
		idx.blocknr = cpu_to_le64(dm_block_location(b));

		r = dm_tm_unlock(io->tm, b);
		if (r < 0)
			return r;

		idx.nr_free = cpu_to_le32(io->entries_per_block);
		idx.none_free_before = 0;
		__dm_bless_for_disk(&idx);

		r = dm_btree_insert(&io->bitmap_info, io->bitmap_root,
				    &i, &idx, &io->bitmap_root);
		if (r < 0)
			return r;
	}

	io->nr_blocks = nr_blocks;
	return 0;
}

static int disk_ll_open(struct ll_disk *ll, struct dm_transaction_manager *tm,
			void *root_le, size_t len)
{
	int r;
	struct disk_sm_root *smr = root_le;

	if (len < sizeof(struct disk_sm_root)) {
		DMERR("sm_disk root too small");
		return -ENOMEM;
	}

	r = disk_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->nr_blocks = le64_to_cpu(smr->nr_blocks);
	ll->nr_allocated = le64_to_cpu(smr->nr_allocated);
	ll->bitmap_root = le64_to_cpu(smr->bitmap_root);
	ll->ref_count_root = le64_to_cpu(smr->ref_count_root);

	return 0;
}

static int disk_ll_lookup_bitmap(struct ll_disk *io, dm_block_t b, uint32_t *result)
{
	int r;
	dm_block_t index = b;
	struct disk_index_entry ie_disk;
	struct dm_block *blk;

	do_div(index, io->entries_per_block);
	r = dm_btree_lookup(&io->bitmap_info, io->bitmap_root, &index, &ie_disk);
	if (r < 0)
		return r;

	r = dm_tm_read_lock(io->tm, le64_to_cpu(ie_disk.blocknr), &dm_sm_bitmap_validator, &blk);
	if (r < 0)
		return r;

	*result = sm_lookup_bitmap(dm_bitmap_data(blk), do_div(b, io->entries_per_block));

	return dm_tm_unlock(io->tm, blk);
}

static int disk_ll_lookup(struct ll_disk *io, dm_block_t b, uint32_t *result)
{
	__le32 rc_le;
	int r = disk_ll_lookup_bitmap(io, b, result);

	if (r)
		return r;

	if (*result != 3)
		return r;

	r = dm_btree_lookup(&io->ref_count_info, io->ref_count_root, &b, &rc_le);
	if (r < 0)
		return r;

	*result = le32_to_cpu(rc_le);

	return r;
}

static int disk_ll_find_free_block(struct ll_disk *io, dm_block_t begin,
				   dm_block_t end, dm_block_t *result)
{
	int r;
	struct disk_index_entry ie_disk;
	dm_block_t i, index_begin = begin;
	dm_block_t index_end = dm_sector_div_up(end, io->entries_per_block);

	begin = do_div(index_begin, io->entries_per_block);

	for (i = index_begin; i < index_end; i++, begin = 0) {
		struct dm_block *blk;
		unsigned position;
		uint32_t bit_end;

		r = dm_btree_lookup(&io->bitmap_info, io->bitmap_root, &i, &ie_disk);
		if (r < 0)
			return r;

		if (le32_to_cpu(ie_disk.nr_free) <= 0)
			continue;

		r = dm_tm_read_lock(io->tm, le64_to_cpu(ie_disk.blocknr),
				    &dm_sm_bitmap_validator, &blk);
		if (r < 0)
			return r;

		bit_end = (i == index_end - 1) ?
			do_div(end, io->entries_per_block) : io->entries_per_block;

		r = sm_find_free(dm_bitmap_data(blk),
				 max((unsigned)begin, (unsigned)le32_to_cpu(ie_disk.none_free_before)),
				 bit_end, &position);
		if (r < 0) {
			dm_tm_unlock(io->tm, blk);
			continue;
		}

		r = dm_tm_unlock(io->tm, blk);
		if (r < 0)
			return r;

		*result = i * io->entries_per_block + (dm_block_t) position;

		return 0;
	}

	return -ENOSPC;
}

static int disk_ll_insert(struct ll_disk *io, dm_block_t b, uint32_t ref_count)
{
	int r;
	uint32_t bit, old;
	struct dm_block *nb;
	dm_block_t index = b;
	struct disk_index_entry ie_disk;
	void *bm_le;
	int inc;

	do_div(index, io->entries_per_block);
	r = dm_btree_lookup(&io->bitmap_info, io->bitmap_root, &index, &ie_disk);
	if (r < 0)
		return r;

	r = dm_tm_shadow_block(io->tm, le64_to_cpu(ie_disk.blocknr),
			       &dm_sm_bitmap_validator, &nb, &inc);
	if (r < 0) {
		DMERR("dm_tm_shadow_block() failed");
		return r;
	}
	ie_disk.blocknr = cpu_to_le64(dm_block_location(nb));

	bm_le = dm_bitmap_data(nb);
	bit = do_div(b, io->entries_per_block);
	old = sm_lookup_bitmap(bm_le, bit);

	if (ref_count <= 2) {
		sm_set_bitmap(bm_le, bit, ref_count);

		if (old > 2) {
			r = dm_btree_remove(&io->ref_count_info, io->ref_count_root,
					    &b, &io->ref_count_root);
			if (r) {
				dm_tm_unlock(io->tm, nb);
				return r;
			}
		}
	} else {
		__le32 rc_le = cpu_to_le32(ref_count);

		__dm_bless_for_disk(&rc_le);

		sm_set_bitmap(bm_le, bit, 3);
		r = dm_btree_insert(&io->ref_count_info, io->ref_count_root,
				    &b, &rc_le, &io->ref_count_root);
		if (r < 0) {
			dm_tm_unlock(io->tm, nb);
			DMERR("ref count insert failed");
			return r;
		}
	}

	r = dm_tm_unlock(io->tm, nb);
	if (r < 0)
		return r;

	if (ref_count && !old) {
		io->nr_allocated++;
		ie_disk.nr_free = cpu_to_le32(le32_to_cpu(ie_disk.nr_free) - 1);
		if (le32_to_cpu(ie_disk.none_free_before) == b)
			ie_disk.none_free_before = cpu_to_le32(b + 1);

	} else if (old && !ref_count) {
		io->nr_allocated--;
		ie_disk.nr_free = cpu_to_le32(le32_to_cpu(ie_disk.nr_free) + 1);
		ie_disk.none_free_before = cpu_to_le32(min((dm_block_t) le32_to_cpu(ie_disk.none_free_before), b));
	}

	__dm_bless_for_disk(&ie_disk);

	r = dm_btree_insert(&io->bitmap_info, io->bitmap_root, &index, &ie_disk, &io->bitmap_root);
	if (r < 0)
		return r;

	return 0;
}

static int disk_ll_inc(struct ll_disk *ll, dm_block_t b)
{
	int r;
	uint32_t rc;

	r = disk_ll_lookup(ll, b, &rc);
	if (r)
		return r;

	return disk_ll_insert(ll, b, rc + 1);
}

static int disk_ll_dec(struct ll_disk *ll, dm_block_t b)
{
	int r;
	uint32_t rc;

	r = disk_ll_lookup(ll, b, &rc);
	if (r)
		return r;

	if (!rc)
		return -EINVAL;

	return disk_ll_insert(ll, b, rc - 1);
}

/*--------------------------------------------------------------*/

/*
 * Space map interface.
 */
struct sm_disk {
	struct dm_space_map sm;

	struct ll_disk ll;
};

static void sm_disk_destroy(struct dm_space_map *sm)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	kfree(smd);
}

static int sm_disk_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	return disk_ll_extend(&smd->ll, extra_blocks);
}

static int sm_disk_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	*count = smd->ll.nr_blocks;

	return 0;
}

static int sm_disk_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	*count = smd->ll.nr_blocks - smd->ll.nr_allocated;

	return 0;
}

static int sm_disk_get_count(struct dm_space_map *sm, dm_block_t b,
			     uint32_t *result)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	return disk_ll_lookup(&smd->ll, b, result);
}

static int sm_disk_count_is_more_than_one(struct dm_space_map *sm, dm_block_t b,
					  int *result)
{
	int r;
	uint32_t count;

	r = sm_disk_get_count(sm, b, &count);
	if (r)
		return r;

	return count > 1;
}

static int sm_disk_set_count(struct dm_space_map *sm, dm_block_t b,
			     uint32_t count)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	return disk_ll_insert(&smd->ll, b, count);
}

static int sm_disk_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	return disk_ll_inc(&smd->ll, b);
}

static int sm_disk_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	return disk_ll_dec(&smd->ll, b);
}

static int sm_disk_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	int r;
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	/*
	 * FIXME: We should start the search where we left off.
	 */
	r = disk_ll_find_free_block(&smd->ll, 0, smd->ll.nr_blocks, b);
	if (r)
		return r;

	return disk_ll_inc(&smd->ll, *b);
}

static int sm_disk_commit(struct dm_space_map *sm)
{
	return 0;
}

static int sm_disk_root_size(struct dm_space_map *sm, size_t *result)
{
	*result = sizeof(struct disk_sm_root);

	return 0;
}

static int sm_disk_copy_root(struct dm_space_map *sm, void *where_le, size_t max)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);
	struct disk_sm_root root_le;

	root_le.nr_blocks = cpu_to_le64(smd->ll.nr_blocks);
	root_le.nr_allocated = cpu_to_le64(smd->ll.nr_allocated);
	root_le.bitmap_root = cpu_to_le64(smd->ll.bitmap_root);
	root_le.ref_count_root = cpu_to_le64(smd->ll.ref_count_root);

	if (max < sizeof(root_le))
		return -ENOSPC;

	memcpy(where_le, &root_le, sizeof(root_le));

	return 0;
}

/*----------------------------------------------------------------*/

static struct dm_space_map ops = {
	.destroy = sm_disk_destroy,
	.extend = sm_disk_extend,
	.get_nr_blocks = sm_disk_get_nr_blocks,
	.get_nr_free = sm_disk_get_nr_free,
	.get_count = sm_disk_get_count,
	.count_is_more_than_one = sm_disk_count_is_more_than_one,
	.set_count = sm_disk_set_count,
	.inc_block = sm_disk_inc_block,
	.dec_block = sm_disk_dec_block,
	.new_block = sm_disk_new_block,
	.commit = sm_disk_commit,
	.root_size = sm_disk_root_size,
	.copy_root = sm_disk_copy_root
};

struct dm_space_map *dm_sm_disk_create(struct dm_transaction_manager *tm,
				       dm_block_t nr_blocks)
{
	int r;
	struct sm_disk *smd;

	smd = kmalloc(sizeof(*smd), GFP_KERNEL);
	if (!smd)
		return ERR_PTR(-ENOMEM);

	memcpy(&smd->sm, &ops, sizeof(smd->sm));

	r = disk_ll_new(&smd->ll, tm);
	if (r)
		goto bad;

	r = disk_ll_extend(&smd->ll, nr_blocks);
	if (r)
		goto bad;

	r = sm_disk_commit(&smd->sm);
	if (r)
		goto bad;

	return &smd->sm;

bad:
	kfree(smd);
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_sm_disk_create);

struct dm_space_map *dm_sm_disk_open(struct dm_transaction_manager *tm,
				     void *root_le, size_t len)
{
	int r;
	struct sm_disk *smd;

	smd = kmalloc(sizeof(*smd), GFP_KERNEL);
	if (!smd)
		return ERR_PTR(-ENOMEM);

	memcpy(&smd->sm, &ops, sizeof(smd->sm));

	r = disk_ll_open(&smd->ll, tm, root_le, len);
	if (r)
		goto bad;

	r = sm_disk_commit(&smd->sm);
	if (r)
		goto bad;

	return &smd->sm;

bad:
	kfree(smd);
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_sm_disk_open);
