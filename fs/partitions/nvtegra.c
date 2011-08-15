/*
 *  fs/partitions/nvtegria.c
 *  Copyright (c) 2010 Gilles Grandou
 *
 *  Nvidia uses for its Tegra2 SOCs a proprietary partition system which is 
 *  unfortunately undocumented.
 *
 *  Typically a Tegra2 system embedds an internal Flash memory (MTD or MMC).
 *  The bottom of this memory contains the initial bootstrap code which 
 *  implements a communication protocol (typically over usb) which allows a 
 *  host system (through a tool called nvflash) to access, read, write and 
 *  partition the internal flash.
 *
 *  The partition table format is not publicaly documented, and usually 
 *  partition description is passed to kernel through the command line
 *  (with tegrapart= argument whose support is available in nv-tegra tree,
 *  see http://nv-tegra.nvidia.com/ )
 *
 *  Rewriting partition table or even switching to a standard msdos is 
 *  theorically possible, but it would mean loosing support from nvflash 
 *  and from bootloader, while no real alternative exists yet.
 *
 *  Partition table format has been reverse-engineered from analysis of
 *  an existing partition table as found on Toshiba AC100/Dynabook AZ. All 
 *  fields have been guessed and there is no guarantee that it will work 
 *  in all situation nor in all other Tegra2 based products.
 *
 *
 *  The standard partitions which can be found on an AC100 are the next 
 *  ones:
 *
 *  sector size = 2048 bytes
 *
 *  Id  Name    Start   Size            Comment
 *              sector  sectors
 *
 *  1           0       1024            unreachable (bootstrap ?)
 *  2   BCT     1024    512             Boot Configuration Table
 *  3   PT      1536    256             Partition Table
 *  4   EBT     1792    1024            Boot Loader
 *  5   SOS     2816    2560            Recovery Kernel
 *  6   LNX     5376    4096            System Kernel
 *  7   MBR     9472    512             MBR - msdos partition table 
 *                                      for the rest of the disk
 *  8   APP     9984    153600          OS root filesystem
 *  ...
 * 
 *  the 1024 first sectors are hidden to the hardware one booted
 *  (so 1024 should be removed from numbers found in the partition
 *  table)
 *
 */

#include "check.h"
#include "nvtegra.h"

#include <linux/types.h>

#define NVPART_NAME_LEN 4
#define NVTEGRA_PT_MAGIC 0xffffffff8f9e8d8bLLU
#define NVTEGRA_PT_OFFSET 0x100000

typedef struct {
	u32 id;
	char name[NVPART_NAME_LEN];
	u32 dev_type;
	u32 dev_id;
	u32 unknown1;
	char name2[NVPART_NAME_LEN];
	u32 fstype;
	u32 fsattr;
	
	u32 part_attr;
	u64 start_sector;
	u64 num_sectors;
	u32 unknown2[4];
	u32 parttype;
} nvtegra_partinfo;

typedef struct {
	u64 magic;
	u32 version;
	u32 length;
	char hash[16];
} nvtegra_hdr_short;

typedef struct {
	u8 trash[16];
	u64 magic;
	u32 version;
	u32 length;
	u32 num_parts;
	char unknown[4];
} nvtegra_hdr_long;

typedef struct {
	nvtegra_hdr_short hdr_s;
	nvtegra_hdr_long hdr_l;
	nvtegra_partinfo parts[];
} nvtegra_ptable;

typedef struct {
	u32 valid;
	char name[4];
	u64 start;
	u64 size;
} temp_partinfo;

char *hidden_parts_str = CONFIG_NVTEGRA_HIDE_PARTS;

static size_t
read_dev_bytes(struct block_device *bdev, unsigned sector, char *buffer,
	       size_t count)
{
	size_t totalreadcount = 0;

	if (!bdev || !buffer)
		return 0;

	while (count) {
		int copied = 512;
		Sector sect;
		unsigned char *data = read_dev_sector(bdev, sector++, &sect);
		if (!data)
			break;
		if (copied > count)
			copied = count;
		memcpy(buffer, data, copied);
		put_dev_sector(sect);
		buffer += copied;
		totalreadcount += copied;
		count -= copied;
	}
	return totalreadcount;
}

int nvtegra_partition(struct parsed_partitions *state)
{
	nvtegra_ptable *pt;
	nvtegra_partinfo *p;
	temp_partinfo *parts;
	temp_partinfo *part;

	int count;
	int i;
	unsigned n_parts;
	u64 pt_offset = 0, offset;
	char *s;

	pt = kzalloc(2048, GFP_KERNEL);
	if (!pt)
		return -1;

	if (read_dev_bytes(state->bdev, 2048, (char *)pt, 2048) != 2048) {
		printk(KERN_INFO "%s: failed to read partition table\n", __func__);
		kfree(pt);
		return 0;
	}

	/* check if partition table looks correct */
	if (pt->hdr_s.magic != pt->hdr_l.magic) {
		printk(KERN_INFO "%s: magic values in headers do not match\n", __func__);
		return 0;
	}

	if (pt->hdr_s.magic != NVTEGRA_PT_MAGIC) {
		printk(KERN_INFO "%s: magic values are wrong\n", __func__);
		return 0;
	}

	if (pt->hdr_s.version != pt->hdr_l.version) {
		printk(KERN_INFO "%s: version mismatch in headers\n", __func__);
		return 0;
	}

	if (pt->hdr_s.version != 0x100) {
		printk(KERN_INFO "%s: unsupported version 0x%x\n",
			__func__, pt->hdr_s.version);
		return 0;
	}

	if (pt->hdr_s.length != pt->hdr_l.length) {
		printk(KERN_INFO "%s: length mismatch in headers\n", __func__);
		return 0;
	}

	n_parts = pt->hdr_l.num_parts;

	printk(KERN_INFO "%s: partition table with %d partitions\n",
		__func__, n_parts);

	parts = kzalloc(n_parts * sizeof(temp_partinfo), GFP_KERNEL);
	if (!parts)
		return -1;

	/* for some reason, the BCT size is incorrectly reported as too large
	 * and other partitions are shifted down.
	 * from observation, PT usually starts at 0x100000 so let's exploit that */
	for (i = 0; i < n_parts; i++) {
		if (!strcmp(pt->parts[i].name, "PT")) {
			pt_offset = (pt->parts[i].start_sector << 12) - NVTEGRA_PT_OFFSET;
			break;
		}
	}

	/* walk the partition table */
	p = pt->parts;
	part = parts;

	for (i = 0; i < n_parts; i++) {
		if (!strcmp(p->name, "BCT")) {
			offset = 0;
		}
		else {
			offset = pt_offset;
		}
	
		printk(KERN_INFO
			"nvtegrapart: [%-4.4s] start=%llu size=%llu\n",
			p->name,
			(p->start_sector << 12) - offset,
			p->num_sectors << 12);
		
		memcpy(part->name, p->name, NVPART_NAME_LEN);
		part->valid = 1;
		part->start = (p->start_sector << 3) - (offset >> 9);
		part->size = (p->num_sectors << 3);
		p++;
		part++;
	}

	/* hide partitions */
	s = hidden_parts_str;
	while (*s) {
		unsigned len;

		len = strcspn(s, ",: ");
		part = parts;

		for (i = 0; i < n_parts; i++) {
			if (part->valid) {
				if (!strncmp(part->name, s, len)
				    && ((len >= NVPART_NAME_LEN)
					|| (part->name[len] == '\0'))) {
					part->valid = 0;
					break;
				}
			}
			part++;
		}
		s += len;
		s += strspn(s, ",: ");
	}

	if (*hidden_parts_str)
		printk(KERN_INFO "\n");
	printk(KERN_INFO "nvtegrapart: hidden_parts = %s\n", hidden_parts_str);

	/* finally register valid partitions */
	count = 1;
	part = parts;
	for (i = 0; i < n_parts; i++) {
		if (part->valid) {
			put_partition(state, count++, part->start, part->size);
		}
		part++;
	}

	kfree(parts);
	kfree(pt);
	return 1;
}

static int __init nvtegra_hideparts_setup(char *options)
{
	if (options)
		hidden_parts_str = options;
	return 0;
}

__setup("nvtegra_hideparts=", nvtegra_hideparts_setup);
