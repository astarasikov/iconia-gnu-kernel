/*
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_BLOCK_MANAGER_H
#define _LINUX_DM_BLOCK_MANAGER_H

#include <linux/blkdev.h>
#include <linux/types.h>
#include <linux/crc32c.h>

/*----------------------------------------------------------------*/

/*
 * Block number.
 */
typedef uint64_t dm_block_t;

/*
 * An opaque handle to a block of data.
 */
struct dm_block;

dm_block_t dm_block_location(struct dm_block *b);
void *dm_block_data(struct dm_block *b);

/*
 * Use CRC32 checksumming on data blocks.
 */
static inline uint32_t dm_block_csum_data(const void *data_le, unsigned length)
{
	return crc32c(~(u32)0, data_le, length);
}

/*----------------------------------------------------------------*/

struct dm_block_manager;
struct dm_block_manager *dm_block_manager_create(struct block_device *bdev, unsigned block_size, unsigned cache_size);
void dm_block_manager_destroy(struct dm_block_manager *bm);

unsigned dm_bm_block_size(struct dm_block_manager *bm);
dm_block_t dm_bm_nr_blocks(struct dm_block_manager *bm);

/*----------------------------------------------------------------*/

/*
 * The validator allows the caller to verify newly-read data and modify
 * the data just before writing, e.g. to calculate checksums.  It's
 * important to be consistent with your use of validators.  The only time
 * you can change validators is if you call dm_bm_write_lock_zero.
 */
struct dm_block_validator {
	const char *name;
	void (*prepare_for_write)(struct dm_block_validator *v, struct dm_block *b, size_t block_size);

	/*
	 * Return 0 if the checksum is valid or < 0 on error.
	 */
	int (*check)(struct dm_block_validator *v, struct dm_block *b, size_t block_size);
};

/*----------------------------------------------------------------*/

/*
 * You can have multiple concurrent readers or a single writer holding a
 * block lock.
 */

/*
 * dm_bm_lock() locks a block and returns through @result a pointer to
 * memory that holds a copy of that block.  If you have write-locked the
 * block then any changes you make to memory pointed to by @result will be
 * written back to the disk sometime after dm_bm_unlock is called.
 */
int dm_bm_read_lock(struct dm_block_manager *bm, dm_block_t b,
		    struct dm_block_validator *v,
		    struct dm_block **result);

int dm_bm_write_lock(struct dm_block_manager *bm, dm_block_t b,
		     struct dm_block_validator *v,
		     struct dm_block **result);

/*
 * The *_try_lock variants return -EWOULDBLOCK if the block isn't
 * available immediately.
 */
int dm_bm_read_try_lock(struct dm_block_manager *bm, dm_block_t b,
			struct dm_block_validator *v,
			struct dm_block **result);

/*
 * Use dm_bm_write_lock_zero() when you know you're going to
 * overwrite the block completely.  It saves a disk read.
 */
int dm_bm_write_lock_zero(struct dm_block_manager *bm, dm_block_t b,
			  struct dm_block_validator *v,
			  struct dm_block **result);

int dm_bm_unlock(struct dm_block *b);

/*
 * It's a common idiom to have a superblock that should be committed last.
 *
 * @superblock should be write-locked on entry. It will be unlocked during
 * this function.  All dirty blocks are guaranteed to be written and flushed
 * before the superblock.
 *
 * This method always blocks.
 */
int dm_bm_flush_and_unlock(struct dm_block_manager *bm,
			   struct dm_block *superblock);

/*
 * The client may wish to change the block device to which the block
 * manager points.  If you use this function then the cache remains intact,
 * so the data must be identical on the both devices, e.g. a different
 * path to the same disk, and it must be at least as big.
 *
 * This function guarantees that once it returns, no further IO will occur
 * on the old device.
 */
int dm_bm_rebind_block_device(struct dm_block_manager *bm,
			      struct block_device *bdev);

#endif	/* _LINUX_DM_BLOCK_MANAGER_H */
