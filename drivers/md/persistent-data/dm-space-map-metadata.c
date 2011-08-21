/*
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-space-map.h"
#include "dm-space-map-common.h"
#include "dm-space-map-metadata.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "space map metadata"

/*----------------------------------------------------------------*/

/*
 * Index validator.
 */
static void index_prepare_for_write(struct dm_block_validator *v,
				    struct dm_block *b,
				    size_t block_size)
{
	struct disk_metadata_index *mi_le = dm_block_data(b);

	mi_le->blocknr = cpu_to_le64(dm_block_location(b));
	mi_le->csum = cpu_to_le32(dm_block_csum_data(&mi_le->padding, block_size - sizeof(__le32)));
}

static int index_check(struct dm_block_validator *v,
		       struct dm_block *b,
		       size_t block_size)
{
	struct disk_metadata_index *mi_le = dm_block_data(b);
	__le32 csum_disk;

	if (dm_block_location(b) != le64_to_cpu(mi_le->blocknr)) {
		DMERR("index_check failed blocknr %llu wanted %llu",
		      le64_to_cpu(mi_le->blocknr), dm_block_location(b));
		return -ENOTBLK;
	}

	csum_disk = cpu_to_le32(dm_block_csum_data(&mi_le->padding,
						 block_size - sizeof(__le32)));
	if (csum_disk != mi_le->csum) {
		DMERR("index_check failed csum %u wanted %u",
		      le32_to_cpu(csum_disk), le32_to_cpu(mi_le->csum));
		return -EILSEQ;
	}

	return 0;
}

static struct dm_block_validator index_validator = {
	.name = "index",
	.prepare_for_write = index_prepare_for_write,
	.check = index_check
};

/*----------------------------------------------------------------*/

/*
 * Low-level disk ops.
 */
static int metadata_ll_init(struct ll_disk *ll, struct dm_transaction_manager *tm)
{
	ll->tm = tm;

	ll->ref_count_info.tm = tm;
	ll->ref_count_info.levels = 1;
	ll->ref_count_info.value_type.size = sizeof(uint32_t);
	ll->ref_count_info.value_type.inc = NULL;
	ll->ref_count_info.value_type.dec = NULL;
	ll->ref_count_info.value_type.equal = NULL;

	ll->block_size = dm_bm_block_size(dm_tm_get_bm(tm));

	if (ll->block_size > (1 << 30)) {
		DMERR("block size too big to hold bitmaps");
		return -EINVAL;
	}

	ll->entries_per_block = (ll->block_size - sizeof(struct disk_bitmap_header)) *
				ENTRIES_PER_BYTE;
	ll->nr_blocks = 0;
	ll->bitmap_root = 0;
	ll->ref_count_root = 0;

	return 0;
}

static int metadata_ll_new(struct ll_disk *ll, struct dm_transaction_manager *tm,
			   dm_block_t nr_blocks)
{
	int r;
	dm_block_t i;
	unsigned blocks;
	struct dm_block *index_block;

	r = metadata_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->nr_blocks = nr_blocks;
	ll->nr_allocated = 0;

	blocks = dm_sector_div_up(nr_blocks, ll->entries_per_block);
	if (blocks > MAX_METADATA_BITMAPS) {
		DMERR("metadata device too large");
		return -EINVAL;
	}

	for (i = 0; i < blocks; i++) {
		struct dm_block *b;
		struct disk_index_entry *idx_le = ll->mi_le.index + i;

		r = dm_tm_new_block(tm, &dm_sm_bitmap_validator, &b);
		if (r < 0)
			return r;
		idx_le->blocknr = cpu_to_le64(dm_block_location(b));

		r = dm_tm_unlock(tm, b);
		if (r < 0)
			return r;

		idx_le->nr_free = cpu_to_le32(ll->entries_per_block);
		idx_le->none_free_before = 0;
	}

	/*
	 * Write the index.
	 */
	r = dm_tm_new_block(tm, &index_validator, &index_block);
	if (r)
		return r;

	ll->bitmap_root = dm_block_location(index_block);
	memcpy(dm_block_data(index_block), &ll->mi_le, sizeof(ll->mi_le));
	r = dm_tm_unlock(tm, index_block);
	if (r)
		return r;

	r = dm_btree_empty(&ll->ref_count_info, &ll->ref_count_root);
	if (r < 0)
		return r;

	return 0;
}

static int metadata_ll_open(struct ll_disk *ll, struct dm_transaction_manager *tm,
			    void *root_le, size_t len)
{
	int r;
	struct disk_sm_root *smr = root_le;
	struct dm_block *block;

	if (len < sizeof(struct disk_sm_root)) {
		DMERR("sm_metadata root too small");
		return -ENOMEM;
	}

	r = metadata_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->nr_blocks = le64_to_cpu(smr->nr_blocks);
	ll->nr_allocated = le64_to_cpu(smr->nr_allocated);
	ll->bitmap_root = le64_to_cpu(smr->bitmap_root);

	r = dm_tm_read_lock(tm, le64_to_cpu(smr->bitmap_root),
			    &index_validator, &block);
	if (r)
		return r;

	memcpy(&ll->mi_le, dm_block_data(block), sizeof(ll->mi_le));
	r = dm_tm_unlock(tm, block);
	if (r)
		return r;

	ll->ref_count_root = le64_to_cpu(smr->ref_count_root);
	return 0;
}

static int metadata_ll_lookup_bitmap(struct ll_disk *ll, dm_block_t b, uint32_t *result)
{
	int r;
	dm_block_t index = b;
	struct disk_index_entry *ie_disk;
	struct dm_block *blk;

	b = do_div(index, ll->entries_per_block);
	ie_disk = ll->mi_le.index + index;

	r = dm_tm_read_lock(ll->tm, le64_to_cpu(ie_disk->blocknr),
			    &dm_sm_bitmap_validator, &blk);
	if (r < 0)
		return r;

	*result = sm_lookup_bitmap(dm_bitmap_data(blk), b);

	return dm_tm_unlock(ll->tm, blk);
}

static int metadata_ll_lookup(struct ll_disk *ll, dm_block_t b, uint32_t *result)
{
	__le32 le_rc;
	int r = metadata_ll_lookup_bitmap(ll, b, result);

	if (r)
		return r;

	if (*result != 3)
		return r;

	r = dm_btree_lookup(&ll->ref_count_info, ll->ref_count_root, &b, &le_rc);
	if (r < 0)
		return r;

	*result = le32_to_cpu(le_rc);

	return r;
}

static int metadata_ll_find_free_block(struct ll_disk *ll, dm_block_t begin,
				       dm_block_t end, dm_block_t *result)
{
	int r;
	struct disk_index_entry *ie_disk;
	dm_block_t i, index_begin = begin;
	dm_block_t index_end = dm_sector_div_up(end, ll->entries_per_block);

	/*
	 * FIXME: Use shifts
	 */
	begin = do_div(index_begin, ll->entries_per_block);
	end = do_div(end, ll->entries_per_block);

	for (i = index_begin; i < index_end; i++, begin = 0) {
		struct dm_block *blk;
		unsigned position;
		uint32_t bit_end;

		ie_disk = ll->mi_le.index + i;

		if (le32_to_cpu(ie_disk->nr_free) <= 0)
			continue;

		r = dm_tm_read_lock(ll->tm, le64_to_cpu(ie_disk->blocknr),
				    &dm_sm_bitmap_validator, &blk);
		if (r < 0)
			return r;

		bit_end = (i == index_end - 1) ?  end : ll->entries_per_block;

		r = sm_find_free(dm_bitmap_data(blk), begin, bit_end, &position);
		if (r < 0) {
			dm_tm_unlock(ll->tm, blk);
			/*
			 * Avoiding retry (FIXME: explain why)
			 */
			return r;
		}

		r = dm_tm_unlock(ll->tm, blk);
		if (r < 0)
			return r;

		*result = i * ll->entries_per_block + (dm_block_t) position;

		return 0;
	}

	return -ENOSPC;
}

static int metadata_ll_insert(struct ll_disk *ll, dm_block_t b, uint32_t ref_count)
{
	int r;
	uint32_t bit, old;
	struct dm_block *nb;
	dm_block_t index = b;
	struct disk_index_entry *ie_disk;
	void *bm_le;
	int inc;

	bit = do_div(index, ll->entries_per_block);
	ie_disk = ll->mi_le.index + index;

	r = dm_tm_shadow_block(ll->tm, le64_to_cpu(ie_disk->blocknr),
			       &dm_sm_bitmap_validator, &nb, &inc);
	if (r < 0) {
		DMERR("dm_tm_shadow_block() failed");
		return r;
	}
	ie_disk->blocknr = cpu_to_le64(dm_block_location(nb));

	bm_le = dm_bitmap_data(nb);
	old = sm_lookup_bitmap(bm_le, bit);

	if (ref_count <= 2) {
		sm_set_bitmap(bm_le, bit, ref_count);

		r = dm_tm_unlock(ll->tm, nb);
		if (r < 0)
			return r;

		if (old > 2) {
			r = dm_btree_remove(&ll->ref_count_info,
					    ll->ref_count_root,
					    &b, &ll->ref_count_root);
			if (r) {
				sm_set_bitmap(bm_le, bit, old);
				return r;
			}
		}
	} else {
		__le32 le_rc = cpu_to_le32(ref_count);

		__dm_bless_for_disk(&le_rc);

		sm_set_bitmap(bm_le, bit, 3);
		r = dm_tm_unlock(ll->tm, nb);
		if (r < 0) {
			__dm_unbless_for_disk(&le_rc);
			return r;
		}

		r = dm_btree_insert(&ll->ref_count_info, ll->ref_count_root,
				    &b, &le_rc, &ll->ref_count_root);
		if (r < 0) {
			/* FIXME: release shadow? or assume the whole transaction will be ditched */
			DMERR("ref count insert failed");
			return r;
		}
	}

	if (ref_count && !old) {
		ll->nr_allocated++;
		ie_disk->nr_free = cpu_to_le32(le32_to_cpu(ie_disk->nr_free) - 1);
		if (le32_to_cpu(ie_disk->none_free_before) == b)
			ie_disk->none_free_before = cpu_to_le32(b + 1);
	} else if (old && !ref_count) {
		ll->nr_allocated--;
		ie_disk->nr_free = cpu_to_le32(le32_to_cpu(ie_disk->nr_free) + 1);
		ie_disk->none_free_before = cpu_to_le32(min((dm_block_t) le32_to_cpu(ie_disk->none_free_before), b));
	}

	return 0;
}

static int metadata_ll_inc(struct ll_disk *ll, dm_block_t b)
{
	int r;
	uint32_t rc;

	r = metadata_ll_lookup(ll, b, &rc);
	if (r)
		return r;

	return metadata_ll_insert(ll, b, rc + 1);
}

static int metadata_ll_dec(struct ll_disk *ll, dm_block_t b)
{
	int r;
	uint32_t rc;

	r = metadata_ll_lookup(ll, b, &rc);
	if (r)
		return r;

	if (!rc)
		return -EINVAL;

	return metadata_ll_insert(ll, b, rc - 1);
}

static int metadata_ll_commit(struct ll_disk *ll)
{
	int r, inc;
	struct dm_block *b;

	r = dm_tm_shadow_block(ll->tm, ll->bitmap_root, &index_validator, &b, &inc);
	if (r)
		return r;

	memcpy(dm_block_data(b), &ll->mi_le, sizeof(ll->mi_le));
	ll->bitmap_root = dm_block_location(b);

	return dm_tm_unlock(ll->tm, b);
}

/*----------------------------------------------------------------*/

/*
 * Space map interface.
 *
 * The low level disk format is written using the standard btree and
 * transaction manager.  This means that performing disk operations may
 * cause us to recurse into the space map in order to allocate new blocks.
 * For this reason we have a pool of pre-allocated blocks large enough to
 * service any metadata_ll_disk operation.
 */

/*
 * FIXME: we should calculate this based on the size of the device.
 * Only the metadata space map needs this functionality.
 */
#define MAX_RECURSIVE_ALLOCATIONS 1024

enum block_op_type {
	BOP_INC,
	BOP_DEC
};

struct block_op {
	enum block_op_type type;
	dm_block_t block;
};

struct sm_metadata {
	struct dm_space_map sm;

	struct ll_disk ll;
	struct ll_disk old_ll;

	dm_block_t begin;

	unsigned recursion_count;
	unsigned allocated_this_transaction;
	unsigned nr_uncommitted;
	struct block_op uncommitted[MAX_RECURSIVE_ALLOCATIONS];
};

static int add_bop(struct sm_metadata *smm, enum block_op_type type, dm_block_t b)
{
	struct block_op *op;

	if (smm->nr_uncommitted == MAX_RECURSIVE_ALLOCATIONS) {
		DMERR("too many recursive allocations");
		return -ENOMEM;
	}

	op = smm->uncommitted + smm->nr_uncommitted++;
	op->type = type;
	op->block = b;

	return 0;
}

static int commit_bop(struct sm_metadata *smm, struct block_op *op)
{
	int r = 0;

	switch (op->type) {
	case BOP_INC:
		r = metadata_ll_inc(&smm->ll, op->block);
		break;

	case BOP_DEC:
		r = metadata_ll_dec(&smm->ll, op->block);
		break;
	}

	return r;
}

static void in(struct sm_metadata *smm)
{
	smm->recursion_count++;
}

static int out(struct sm_metadata *smm)
{
	int r = 0;

	/*
	 * If we're not recursing then very bad things are happening.
	 */
	if (!smm->recursion_count) {
		DMERR("lost track of recursion depth");
		return -ENOMEM;
	}

	if (smm->recursion_count == 1 && smm->nr_uncommitted) {
		while (smm->nr_uncommitted && !r) {
			smm->nr_uncommitted--;
			r = commit_bop(smm, smm->uncommitted +
				       smm->nr_uncommitted);
			if (r)
				break;
		}
	}

	smm->recursion_count--;

	return r;
}

/*
 * When using the out() function above, we often want to combine an error
 * code for the operation run in the recursive context with that from
 * out().
 */
static int combine_errors(int r1, int r2)
{
	return r1 ? r1 : r2;
}

static int recursing(struct sm_metadata *smm)
{
	return smm->recursion_count;
}

static void sm_metadata_destroy(struct dm_space_map *sm)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	kfree(smm);
}

static int sm_metadata_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	DMERR("doesn't support extend");
	return -EINVAL;
}

static int sm_metadata_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->ll.nr_blocks;

	return 0;
}

static int sm_metadata_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->old_ll.nr_blocks - smm->old_ll.nr_allocated -
		 smm->allocated_this_transaction;

	return 0;
}

static int sm_metadata_get_count(struct dm_space_map *sm, dm_block_t b,
				 uint32_t *result)
{
	int r, i;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	unsigned adjustment = 0;

	/*
	 * We may have some uncommitted adjustments to add.  This list
	 * should always be really short.
	 */
	for (i = 0; i < smm->nr_uncommitted; i++) {
		struct block_op *op = smm->uncommitted + i;

		if (op->block != b)
			continue;

		switch (op->type) {
		case BOP_INC:
			adjustment++;
			break;

		case BOP_DEC:
			adjustment--;
			break;
		}
	}

	r = metadata_ll_lookup(&smm->ll, b, result);
	if (r)
		return r;

	*result += adjustment;

	return 0;
}

static int sm_metadata_count_is_more_than_one(struct dm_space_map *sm,
					      dm_block_t b, int *result)
{
	int r, i, adjustment = 0;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	uint32_t rc;

	/*
	 * We may have some uncommitted adjustments to add.  This list
	 * should always be really short.
	 */
	for (i = 0; i < smm->nr_uncommitted; i++) {
		struct block_op *op = smm->uncommitted + i;

		if (op->block != b)
			continue;

		switch (op->type) {
		case BOP_INC:
			adjustment++;
			break;

		case BOP_DEC:
			adjustment--;
			break;
		}
	}

	if (adjustment > 1) {
		*result = 1;
		return 0;
	}

	r = metadata_ll_lookup_bitmap(&smm->ll, b, &rc);
	if (r)
		return r;

	if (rc == 3)
		/*
		 * We err on the side of caution, and always return true.
		 */
		*result = 1;
	else
		*result = rc + adjustment > 1;

	return 0;
}

static int sm_metadata_set_count(struct dm_space_map *sm, dm_block_t b,
				 uint32_t count)
{
	int r, r2;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	if (smm->recursion_count) {
		DMERR("cannot recurse set_count()");
		return -EINVAL;
	}

	in(smm);
	r = metadata_ll_insert(&smm->ll, b, count);
	r2 = out(smm);

	return combine_errors(r, r2);
}

static int sm_metadata_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	int r, r2 = 0;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	if (recursing(smm))
		r = add_bop(smm, BOP_INC, b);
	else {
		in(smm);
		r = metadata_ll_inc(&smm->ll, b);
		r2 = out(smm);
	}

	return combine_errors(r, r2);
}

static int sm_metadata_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	int r, r2 = 0;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	if (recursing(smm))
		r = add_bop(smm, BOP_DEC, b);
	else {
		in(smm);
		r = metadata_ll_dec(&smm->ll, b);
		r2 = out(smm);
	}

	return combine_errors(r, r2);
}

static int sm_metadata_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	int r, r2 = 0;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	r = metadata_ll_find_free_block(&smm->old_ll, smm->begin, smm->old_ll.nr_blocks, b);
	if (r)
		return r;

	smm->begin = *b + 1;

	if (recursing(smm))
		r = add_bop(smm, BOP_INC, *b);
	else {
		in(smm);
		r = metadata_ll_inc(&smm->ll, *b);
		r2 = out(smm);
	}

	if (!r)
		smm->allocated_this_transaction++;

	return combine_errors(r, r2);
}

static int sm_metadata_commit(struct dm_space_map *sm)
{
	int r;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	memcpy(&smm->old_ll, &smm->ll, sizeof(smm->old_ll));

	r = metadata_ll_commit(&smm->ll);
	if (r)
		return r;

	memcpy(&smm->old_ll, &smm->ll, sizeof(smm->old_ll));
	smm->begin = 0;
	smm->allocated_this_transaction = 0;

	return 0;
}

static int sm_metadata_root_size(struct dm_space_map *sm, size_t *result)
{
	*result = sizeof(struct disk_sm_root);

	return 0;
}

static int sm_metadata_copy_root(struct dm_space_map *sm, void *where_le, size_t max)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	struct disk_sm_root root_le;

	root_le.nr_blocks = cpu_to_le64(smm->ll.nr_blocks);
	root_le.nr_allocated = cpu_to_le64(smm->ll.nr_allocated);
	root_le.bitmap_root = cpu_to_le64(smm->ll.bitmap_root);
	root_le.ref_count_root = cpu_to_le64(smm->ll.ref_count_root);

	if (max < sizeof(root_le))
		return -ENOSPC;

	memcpy(where_le, &root_le, sizeof(root_le));

	return 0;
}

static struct dm_space_map ops = {
	.destroy = sm_metadata_destroy,
	.extend = sm_metadata_extend,
	.get_nr_blocks = sm_metadata_get_nr_blocks,
	.get_nr_free = sm_metadata_get_nr_free,
	.get_count = sm_metadata_get_count,
	.count_is_more_than_one = sm_metadata_count_is_more_than_one,
	.set_count = sm_metadata_set_count,
	.inc_block = sm_metadata_inc_block,
	.dec_block = sm_metadata_dec_block,
	.new_block = sm_metadata_new_block,
	.commit = sm_metadata_commit,
	.root_size = sm_metadata_root_size,
	.copy_root = sm_metadata_copy_root
};

/*----------------------------------------------------------------*/

/*
 * When a new space map is created that manages its own space.  We use
 * this tiny bootstrap allocator.
 */
static void sm_bootstrap_destroy(struct dm_space_map *sm)
{
}

static int sm_bootstrap_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	DMERR("boostrap doesn't support extend");

	return -EINVAL;
}

static int sm_bootstrap_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	return smm->ll.nr_blocks;
}

static int sm_bootstrap_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->ll.nr_blocks - smm->begin;

	return 0;
}

static int sm_bootstrap_get_count(struct dm_space_map *sm, dm_block_t b,
				  uint32_t *result)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	return b < smm->begin ? 1 : 0;
}

static int sm_bootstrap_count_is_more_than_one(struct dm_space_map *sm,
					       dm_block_t b, int *result)
{
	*result = 0;

	return 0;
}

static int sm_bootstrap_set_count(struct dm_space_map *sm, dm_block_t b,
				  uint32_t count)
{
	DMERR("boostrap doesn't support set_count");

	return -EINVAL;
}

static int sm_bootstrap_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	/*
	 * We know the entire device is unused.
	 */
	if (smm->begin == smm->ll.nr_blocks)
		return -ENOSPC;

	*b = smm->begin++;

	return 0;
}

static int sm_bootstrap_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	return add_bop(smm, BOP_INC, b);
}

static int sm_bootstrap_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	return add_bop(smm, BOP_DEC, b);
}

static int sm_bootstrap_commit(struct dm_space_map *sm)
{
	return 0;
}

static int sm_bootstrap_root_size(struct dm_space_map *sm, size_t *result)
{
	DMERR("boostrap doesn't support root_size");

	return -EINVAL;
}

static int sm_bootstrap_copy_root(struct dm_space_map *sm, void *where,
				  size_t max)
{
	DMERR("boostrap doesn't support copy_root");

	return -EINVAL;
}

static struct dm_space_map bootstrap_ops = {
	.destroy = sm_bootstrap_destroy,
	.extend = sm_bootstrap_extend,
	.get_nr_blocks = sm_bootstrap_get_nr_blocks,
	.get_nr_free = sm_bootstrap_get_nr_free,
	.get_count = sm_bootstrap_get_count,
	.count_is_more_than_one = sm_bootstrap_count_is_more_than_one,
	.set_count = sm_bootstrap_set_count,
	.inc_block = sm_bootstrap_inc_block,
	.dec_block = sm_bootstrap_dec_block,
	.new_block = sm_bootstrap_new_block,
	.commit = sm_bootstrap_commit,
	.root_size = sm_bootstrap_root_size,
	.copy_root = sm_bootstrap_copy_root
};

/*----------------------------------------------------------------*/

struct dm_space_map *dm_sm_metadata_init(void)
{
	struct sm_metadata *smm;

	smm = kmalloc(sizeof(*smm), GFP_KERNEL);
	if (!smm)
		return ERR_PTR(-ENOMEM);

	memcpy(&smm->sm, &ops, sizeof(smm->sm));

	return &smm->sm;
}

int dm_sm_metadata_create(struct dm_space_map *sm,
			  struct dm_transaction_manager *tm,
			  dm_block_t nr_blocks,
			  dm_block_t superblock)
{
	int r;
	dm_block_t i;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	smm->begin = superblock + 1;
	smm->recursion_count = 0;
	smm->allocated_this_transaction = 0;
	smm->nr_uncommitted = 0;

	memcpy(&smm->sm, &bootstrap_ops, sizeof(smm->sm));
	r = metadata_ll_new(&smm->ll, tm, nr_blocks);
	if (r)
		return r;
	memcpy(&smm->sm, &ops, sizeof(smm->sm));

	/*
	 * Now we need to update the newly created data structures with the
	 * allocated blocks that they were built from.
	 */
	for (i = superblock; !r && i < smm->begin; i++)
		r = metadata_ll_inc(&smm->ll, i);

	if (r)
		return r;

	return sm_metadata_commit(sm);
}

int dm_sm_metadata_open(struct dm_space_map *sm,
			struct dm_transaction_manager *tm,
			void *root_le, size_t len)
{
	int r;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	r = metadata_ll_open(&smm->ll, tm, root_le, len);
	if (r)
		return r;

	smm->begin = 0;
	smm->recursion_count = 0;
	smm->allocated_this_transaction = 0;
	smm->nr_uncommitted = 0;

	return sm_metadata_commit(sm);
}
