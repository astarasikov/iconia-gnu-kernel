/*
 * Copyright (C) 2010 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * This file is released under the GPLv2: see the file COPYING for details.
 */

#include <linux/bootmem.h>
#include <linux/debugfs.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/preserved.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>

struct preserved {
	unsigned int	magic;
	unsigned int	cursor;
	char		buf[0];
};

/* This footer structure appears at the end of the preserve area */
struct preserved_ftr {
	unsigned int	ksize;
	unsigned int	usize;
};
static struct preserved *preserved;
static struct preserved_ftr *preserved_ftr;

static bool preserved_was_reserved;
static DEFINE_MUTEX(preserved_mutex);

/* Default start and size of preserved area */
/*
 * The position and size of the buffer in memory are set by:
 *
 *  CONFIG_PRESERVED_RAM_START - default 0x00f00000 on x86 (15MB)
 *  CONFIG_PRESERVED_RAM_SIZE  - default 0x00100000 (1MB)
 *
 */
static unsigned long preserved_start = CONFIG_PRESERVED_RAM_START;
static unsigned long preserved_size = CONFIG_PRESERVED_RAM_SIZE;

static unsigned long preserved_bufsize;

#ifndef CONFIG_NO_BOOTMEM

/* Location of the reserved area for the kcrash buffer */
struct resource kcrash_res = {
	.name  = "Kcrash buffer",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM
};

#endif

/*
 * We avoid writing or reading the preserved area until we have to,
 * so that a kernel with this configured in can be run even on boxes
 * where writing to or reading from that area might cause trouble.
 */
static bool preserved_is_valid(void)
{
	if (preserved_was_reserved &&
	    preserved->magic == DEBUGFS_MAGIC &&
	    preserved->cursor < preserved_bufsize &&
	    preserved_ftr->ksize <= preserved_bufsize &&
	    preserved_ftr->usize <= preserved_bufsize &&
	    preserved_ftr->ksize + preserved_ftr->usize >=
		preserved->cursor &&
	    preserved_ftr->ksize + preserved_ftr->usize <=
		preserved_bufsize)
		return true;

	return false;
}

/*
 * The noinline below works around a compiler bug, which inlined both calls to
 * preserved_make_valid(), but omitted its tail call to preserved_is_valid():
 * so the first write to utrace failed with ENXIO, or the first attempt to
 * save kernel crash messages skipped immediately to reboot.
 * FIXME(sjg) do we still suffer from this compiler bug?
 */
static noinline bool preserved_make_valid(void)
{
	if (!preserved_was_reserved)
		return false;

	preserved->magic = DEBUGFS_MAGIC;
	preserved->cursor = 0;
	preserved_ftr->ksize = 0;
	preserved_ftr->usize = 0;

	/*
	 * But perhaps this reserved area is not actually backed by RAM?
	 * Check that we can read back what we wrote - though this check
	 * would be better with a cache flush (dependent on architecture).
	 */
	return preserved_is_valid();
}

/*
 * For runtime: reading and writing /sys/kernel/debug/preserved files.
 */

static ssize_t kcrash_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	unsigned int offset, limit, residue;
	int error = 0;

	mutex_lock(&preserved_mutex);
	if (!preserved_is_valid())
		goto out;
	if (pos < 0 || pos >= preserved_ftr->ksize)
		goto out;
	if (count > preserved_ftr->ksize - pos)
		count = preserved_ftr->ksize - pos;

	offset = preserved->cursor - preserved_ftr->ksize;
	if ((int)offset < 0)
		offset += preserved_bufsize;
	offset += pos;
	if (offset > preserved_bufsize)
		offset -= preserved_bufsize;

	limit = preserved_bufsize - offset;
	residue = count;
	error = -EFAULT;

	if (residue > limit) {
		if (copy_to_user(buf, preserved->buf + offset, limit))
			goto out;
		offset = 0;
		residue -= limit;
		buf += limit;
	}

	if (copy_to_user(buf, preserved->buf + offset, residue))
		goto out;

	pos += count;
	*ppos = pos;
	error = count;
out:
	mutex_unlock(&preserved_mutex);
	return error;
}

static ssize_t kcrash_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	/*
	 * A write to kcrash does nothing but reset both kcrash and utrace.
	 */
	mutex_lock(&preserved_mutex);
	if (preserved_is_valid()) {
		preserved->cursor = 0;
		preserved_ftr->ksize = 0;
		preserved_ftr->usize = 0;
	}
	mutex_unlock(&preserved_mutex);
	return count;
}

static const struct file_operations kcrash_operations = {
	.read	= kcrash_read,
	.write	= kcrash_write,
};

/* FIXME(sjg): very similar to kcrash_read(). Refactor to simplify */

static ssize_t utrace_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	unsigned int offset, limit, residue;
	unsigned int supersize, origin;
	int error = 0;

	/*
	 * Try to handle the case when utrace entries are being added
	 * in between our sequential reads; but if they're being added
	 * faster than we're reading them, this won't work very well.
	 */
	mutex_lock(&preserved_mutex);
	if (!preserved_is_valid())
		goto out;
	supersize = preserved_ftr->usize;

	if (pos == 0 || preserved_ftr->ksize != 0) {
		origin = 0;
		if (supersize == preserved_bufsize - preserved_ftr->ksize)
			origin = preserved->cursor;
		file->private_data = (void *)origin;
	} else {	/* cursor may have moved since we started reading */
		origin = (unsigned int)file->private_data;
		if (supersize == preserved_bufsize) {
			int advance = preserved->cursor - origin;
			if (advance < 0)
				advance += preserved_bufsize;
			supersize += advance;
		}
	}

	if (pos < 0 || pos >= supersize)
		goto out;
	if (count > supersize - pos)
		count = supersize - pos;

	offset = origin + pos;
	if (offset > preserved_bufsize)
		offset -= preserved_bufsize;
	limit = preserved_bufsize - offset;
	residue = count;
	error = -EFAULT;

	if (residue > limit) {
		if (copy_to_user(buf, preserved->buf + offset, limit))
			goto out;
		offset = 0;
		residue -= limit;
		buf += limit;
	}

	if (copy_to_user(buf, preserved->buf + offset, residue))
		goto out;

	pos += count;
	*ppos = pos;
	error = count;
out:
	mutex_unlock(&preserved_mutex);
	return error;
}

static ssize_t utrace_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned int offset, limit, residue;
	unsigned int usize = 0;
	int error;

	/*
	 * Originally, writing to the preserved area was implemented
	 * just for testing that it is all preserved.  But it might be
	 * useful for debugging a kernel crash if we allow userspace
	 * to write trace records to that area as a circular buffer.
	 * But don't allow any utrace writes once a kcrash is present.
	 */
	mutex_lock(&preserved_mutex);
	if (!preserved_is_valid() && !preserved_make_valid()) {
		error = -ENXIO;
		goto out;
	}
	if (preserved_ftr->ksize != 0) {
		error = -ENOSPC;
		goto out;
	}

	if (count > preserved_bufsize) {
		buf += count - preserved_bufsize;
		count = preserved_bufsize;
	}

	offset = preserved->cursor;
	limit = preserved_bufsize - offset;
	residue = count;
	error = -EFAULT;

	if (residue > limit) {
		if (copy_from_user(preserved->buf + offset, buf, limit))
			goto out;
		usize = preserved_bufsize;
		offset = 0;
		residue -= limit;
		buf += limit;
	}

	if (copy_from_user(preserved->buf + offset, buf, residue))
		goto out;

	offset += residue;
	if (usize < offset)
		usize = offset;
	if (preserved_ftr->usize < usize)
		preserved_ftr->usize = usize;
	if (offset == preserved_bufsize)
		offset = 0;
	preserved->cursor = offset;

	/*
	 * We always append, ignoring ppos: don't even pretend to maintain it.
	 */
	error = count;
out:
	mutex_unlock(&preserved_mutex);
	return error;
}

static const struct file_operations utrace_operations = {
	.read	= utrace_read,
	.write	= utrace_write,
};

static void kcrash_append(unsigned int log_size)
{
	int excess = preserved_ftr->usize + preserved_ftr->ksize +
			log_size - preserved_bufsize;

	if (excess <= 0) {
		/* kcrash fits without losing any utrace */
		preserved_ftr->ksize += log_size;
	} else if (excess <= preserved_ftr->usize) {
		/* some of utrace was overwritten by kcrash */
		preserved_ftr->usize -= excess;
		preserved_ftr->ksize += log_size;
	} else {
		/* no utrace left and kcrash is full */
		preserved_ftr->usize = 0;
		preserved_ftr->ksize = preserved_bufsize;
	}

	preserved->cursor += log_size;
	if (preserved->cursor >= preserved_bufsize)
		preserved->cursor -= preserved_bufsize;
}

static void kcrash_preserve(bool first_time)
{
	static unsigned int save_cursor;
	static unsigned int save_ksize;
	static unsigned int save_usize;

	if (first_time) {
		save_cursor = preserved->cursor;
		save_ksize  = preserved_ftr->ksize;
		save_usize  = preserved_ftr->usize;
	} else {
		/*
		 * Restore original cursor etc. so that we can take a fresh
		 * snapshot of the log_buf, including our own error messages,
		 * if something goes wrong in emergency_restart().  This does
		 * assume, reasonably, that log_size will not shrink.
		 */
		preserved->cursor = save_cursor;
		preserved_ftr->ksize  = save_ksize;
		preserved_ftr->usize  = save_usize;
	}

	kcrash_append(copy_log_buf(preserved->buf,
			preserved_bufsize, preserved->cursor));
	pr_debug("preserved: saved, magic=%x, cursor=%x, ksize=%x, usize=%x\n",
		preserved->magic, preserved->cursor,
		preserved_ftr->ksize, preserved_ftr->usize);
}

static void flush_preserved(void)
{
#ifdef CONFIG_X86
	/* flush_cache_all is a nop on x86 */
	wbinvd();
#else
	flush_cache_all();
#endif
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
		kcrash_preserve(true);
		flush_preserved();
	}
	machine_emergency_restart();
}

/*
 * Pick out the preserved memory size.  We look for kcrashmem=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init early_kcrashmem(char *p)
{
	unsigned long size, start;
	char *endp;

	start = 0;
	size  = memparse(p, &endp);
	if (*endp == '@')
		start = memparse(endp + 1, NULL);
	else
		size = 0; /* must specify start to get a valid region */

	/* basic sanity check - both start and size must be page aligned */
	if ((start | size) & (PAGE_SIZE - 1))
		preserved_size = 0;
	else {
		preserved_start = start;
		preserved_size = size;
	}
	return 0;
}
early_param("kcrashmem", early_kcrashmem);

/*
 * Initialization: initialize early (once debugfs is ready) so that we are
 * ready to handle early panics (though S3-reboot can only be set up later).
 */

static bool __init preserved_is_reserved(void)
{
#ifndef CONFIG_NO_BOOTMEM
	/*
	 * Where bootmem is available, we must reserve the memory early in
	 * the boot process. This is done using reserve_bootmem().
	 */
	if (reserve_bootmem(preserved_start, preserved_size,
		BOOTMEM_EXCLUSIVE < 0)) {
		printk(KERN_WARNING "preserved: reservation failed - "
		       "memory is in use (0x%lx)\n", preserved_start);
		preserved_size = 0;
		return 0;
	}
	kcrash_res.start = preserved_start;
	kcrash_res.end = preserved_start + preserved_size - 1;
	insert_resource(&iomem_resource, &kcrash_res);
#elif defined CONFIG_X86
	/* On x86 this memory is assumed already reserved, so check it */
	unsigned int pfn = preserved_start >> PAGE_SHIFT;
	unsigned int efn = pfn + (preserved_size >> PAGE_SHIFT);

	while (pfn < efn) {
		if (!pfn_valid(pfn)) {
			pr_warning("preserved: invalid pfn %#x\n", pfn);
			return false;
		}
		if (!PageReserved(pfn_to_page(pfn))) {
			pr_warning("preserved: page not reserved %#x\n", pfn);
			return false;
		}
		pfn++;
	}
#else
	/* Sadly this architecture does not support preserved memory yet */
	pr_warning("preserved: not supported on this architecture\n");
	return false;
#endif
	preserved_was_reserved = true;
	return true;
}

static int __init preserved_init(void)
{
	struct dentry *dir;

	/*
	 * Whether or not it can preserve an oops or other bug trace, ChromeOS
	 * prefers to reboot the machine immediately when a kernel bug occurs.
	 * It's easier to force these here than insist on more boot options.
	 */
	panic_on_oops = 1;
	panic_timeout = -1;		/* reboot without waiting */

	/* we are only enabled if we have a valid region */
	if (preserved_size == 0)
		return 0;

	/*
	 * Check that the RAM we expect to use has indeed been reserved
	 * for us: this kernel might be running on a machine without it.
	 * But to be even safer, we don't access that memory until asked.
	 */
	if (preserved_is_reserved()) {
		/*
		 * If error occurs in setting up /sys/kernel/debug/preserved/,
		 * we cannot do better than ignore it.
		 */
		dir = debugfs_create_dir("preserved", NULL);
		if (dir && !IS_ERR(dir)) {
			debugfs_create_file("kcrash", S_IFREG|S_IRUSR|S_IWUSR,
						dir, NULL, &kcrash_operations);
			debugfs_create_file("utrace", S_IFREG|S_IRUSR|S_IWUGO,
						dir, NULL, &utrace_operations);
		}

		/* get our pointers set up, now we know where the area is */
		/* FIXME(sjg): change to use ioremap() and accessors */
		preserved = __va(preserved_start);
		preserved_ftr = __va(preserved_start + preserved_size -
			sizeof(*preserved_ftr));
		preserved_bufsize = preserved_size - sizeof(preserved) -
			sizeof(*preserved_ftr);

		pr_info("preserved: reserved %luMB at %#lx (virtual %p)\n",
		       preserved_size >> 20, preserved_start, preserved);
		pr_debug("preserved: magic=%x, cursor=%#x, ksize=%#x, "
			"usize=%#x\n", preserved->magic, preserved->cursor,
			preserved_ftr->ksize, preserved_ftr->usize);
		if (preserved_is_valid())
			pr_debug("preserved: %d bytes of kcrash data "
					"available\n", preserved_ftr->ksize);
	}

	return 0;
}
postcore_initcall(preserved_init);
