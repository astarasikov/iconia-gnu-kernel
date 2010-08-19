/*
 * Copyright (C) 2010 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * This file is released under the GPLv2: see the file COPYING for details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/preserved.h>
#include <linux/reboot.h>

#define CHROMEOS_PRESERVED_RAM_ADDR 0x00f00000	/* 15MB */
#define CHROMEOS_PRESERVED_RAM_SIZE 0x00100000	/*  1MB */
#define CHROMEOS_PRESERVED_BUF_SIZE (CHROMEOS_PRESERVED_RAM_SIZE-4*sizeof(int))

struct preserved {
	unsigned int	magic;
	unsigned int	cursor;
	char		buf[CHROMEOS_PRESERVED_BUF_SIZE];
	unsigned int	ksize;
	unsigned int	pad;		/* up here to verify end of area */
};
static struct preserved *preserved = __va(CHROMEOS_PRESERVED_RAM_ADDR);

static bool preserved_was_reserved;

/*
 * We avoid writing or reading the preserved area until we have to,
 * so that a kernel with this configured in can be run even on boxes
 * where writing to or reading from that area might cause trouble.
 */
static bool preserved_is_valid(void)
{
	BUILD_BUG_ON(sizeof(*preserved) != CHROMEOS_PRESERVED_RAM_SIZE);

	if (preserved_was_reserved &&
	    preserved->magic == DEBUGFS_MAGIC &&
	    preserved->cursor < sizeof(preserved->buf) &&
	    preserved->ksize <= sizeof(preserved->buf) &&
	    preserved->ksize >= preserved->cursor &&
	    preserved->pad == DEBUGFS_MAGIC)
		return true;

	return false;
}

static bool preserved_make_valid(void)
{
	if (!preserved_was_reserved)
		return false;

	preserved->magic = DEBUGFS_MAGIC;
	preserved->cursor = 0;
	preserved->ksize = 0;
	preserved->pad = DEBUGFS_MAGIC;

	/*
	 * But perhaps this reserved area is not actually backed by RAM?
	 * Check that we can read back what we wrote - though this check
	 * would be better with a cache flush (dependent on architecture).
	 */
	return preserved_is_valid();
}

static void kcrash_append(unsigned int log_size)
{
	preserved->ksize += log_size;
	if (preserved->ksize > sizeof(preserved->buf))
		preserved->ksize = sizeof(preserved->buf);

	preserved->cursor += log_size;
	if (preserved->cursor >= sizeof(preserved->buf))
		preserved->cursor -= sizeof(preserved->buf);
}

static void kcrash_preserve(void)
{
	kcrash_append(copy_log_buf(preserved->buf,
			sizeof(preserved->buf), preserved->cursor));
}

void emergency_restart(void)	/* overriding the __weak one in kernel/sys.c */
{
	/*
	 * Initialize a good header if that's not already been done.
	 */
	if (preserved_is_valid() || preserved_make_valid()) {
		printk(KERN_INFO "Preserving kcrash across reboot\n");
		/*
		 * Copy printk's log_buf (kmsg or dmesg) into our preserved buf,
		 * perhaps appending to a kcrash from the previous boot.
		 */
		kcrash_preserve();
	}
	machine_emergency_restart();
}

/*
 * Initialization: initialize early (once debugfs is ready) so that we are
 * ready to handle early panics (though S3-reboot can only be set up later).
 */

static bool __init preserved_is_reserved(void)
{
	unsigned int pfn = CHROMEOS_PRESERVED_RAM_ADDR >> PAGE_SHIFT;
	unsigned int efn = pfn + (CHROMEOS_PRESERVED_RAM_SIZE >> PAGE_SHIFT);

	while (pfn < efn) {
		if (!pfn_valid(pfn))
			return false;
		if (!PageReserved(pfn_to_page(pfn)))
			return false;
		pfn++;
	}

	preserved_was_reserved = true;
	return true;
}

static int __init preserved_init(void)
{
	/*
	 * Whether or not it can preserve an oops or other bug trace, ChromeOS
	 * prefers to reboot the machine immediately when a kernel bug occurs.
	 * It's easier to force these here than insist on more boot options.
	 */
	panic_on_oops = 1;
	panic_timeout = -1;		/* reboot without waiting */

	/*
	 * Check that the RAM we expect to use has indeed been reserved
	 * for us: this kernel might be running on a machine without it.
	 * But to be even safer, we don't access that memory until asked.
	 */
	preserved_is_reserved();
	return 0;
}
postcore_initcall(preserved_init);
