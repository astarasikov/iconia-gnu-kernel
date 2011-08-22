/*
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_SPACE_MAP_COMMON_H
#define DM_SPACE_MAP_COMMON_H

#include "dm-btree.h"

/*
 *--------------------------------------------------------------------
 * Low level disk format
 *
 * Bitmap btree
 * ------------
 *
 * Each value stored in the btree is an index_entry.  This points to a
 * block that is used as a bitmap.  Within the bitmap hold 2 bits per
 * entry, which represent UNUSED = 0, REF_COUNT = 1, REF_COUNT = 2 and
 * REF_COUNT = many.
 *
 * Refcount btree
 * --------------
 *
 * Any entry that has a ref count higher than 2 gets entered in the ref
 * count tree.  The leaf values for this tree is the 32-bit ref count.
 *---------------------------------------------------------------------
 */

struct disk_index_entry {
	__le64 blocknr;
	__le32 nr_free;
	__le32 none_free_before;
} __packed;


#define MAX_METADATA_BITMAPS 255
struct disk_metadata_index {
	__le32 csum;
	__le32 padding;
	__le64 blocknr;

	struct disk_index_entry index[MAX_METADATA_BITMAPS];
} __packed;

struct ll_disk {
	struct dm_transaction_manager *tm;
	struct dm_btree_info bitmap_info;
	struct dm_btree_info ref_count_info;

	uint32_t block_size;
	uint32_t entries_per_block;
	dm_block_t nr_blocks;
	dm_block_t nr_allocated;

	/*
	 * bitmap_root may be a btree root or a simple index.
	 */
	dm_block_t bitmap_root;

	dm_block_t ref_count_root;

	struct disk_metadata_index mi_le;
};

struct disk_sm_root {
	__le64 nr_blocks;
	__le64 nr_allocated;
	__le64 bitmap_root;
	__le64 ref_count_root;
} __packed;

#define ENTRIES_PER_BYTE 4

struct disk_bitmap_header {
	__le32 csum;
	__le32 not_used;
	__le64 blocknr;
} __packed;

/*
 * These bitops work on a block's worth of bits.
 */
unsigned sm_lookup_bitmap(void *addr, unsigned b);
void sm_set_bitmap(void *addr, unsigned b, unsigned val);
int sm_find_free(void *addr, unsigned begin, unsigned end, unsigned *result);

void *dm_bitmap_data(struct dm_block *b);

extern struct dm_block_validator dm_sm_bitmap_validator;

#endif	/* DM_SPACE_MAP_COMMON_H */
