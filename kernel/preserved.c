/*
 * Copyright (C) 2010 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * This file is released under the GPLv2: see the file COPYING for details.
 */

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/page-flags.h>
#include <linux/preserved.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>

#define CHROMEOS_PRESERVED_RAM_ADDR 0x00f00000	/* 15MB */
#define CHROMEOS_PRESERVED_RAM_SIZE 0x00100000	/*  1MB */
#define CHROMEOS_PRESERVED_BUF_SIZE (CHROMEOS_PRESERVED_RAM_SIZE-4*sizeof(int))

struct preserved {
	unsigned int	magic;
	unsigned int	cursor;
	char		buf[CHROMEOS_PRESERVED_BUF_SIZE];
	unsigned int	ksize;
	unsigned int	usize;		/* up here to verify end of area */
};
static struct preserved *preserved = __va(CHROMEOS_PRESERVED_RAM_ADDR);

static bool preserved_was_reserved;
static DEFINE_MUTEX(preserved_mutex);

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
	    preserved->usize <= sizeof(preserved->buf) &&
	    preserved->ksize + preserved->usize >= preserved->cursor &&
	    preserved->ksize + preserved->usize <= sizeof(preserved->buf))
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
	preserved->usize = 0;

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
	if (pos < 0 || pos >= preserved->ksize)
		goto out;
	if (count > preserved->ksize - pos)
		count = preserved->ksize - pos;

	offset = preserved->cursor - preserved->ksize;
	if ((int)offset < 0)
		offset += sizeof(preserved->buf);
	offset += pos;
	if (offset > sizeof(preserved->buf))
		offset -= sizeof(preserved->buf);

	limit = sizeof(preserved->buf) - offset;
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
		preserved->ksize = 0;
		preserved->usize = 0;
	}
	mutex_unlock(&preserved_mutex);
	return count;
}

static const struct file_operations kcrash_operations = {
	.read	= kcrash_read,
	.write	= kcrash_write,
};

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
	supersize = preserved->usize;

	if (pos == 0 || preserved->ksize != 0) {
		origin = 0;
		if (supersize == sizeof(preserved->buf) - preserved->ksize)
			origin = preserved->cursor;
		file->private_data = (void *)origin;
	} else {	/* cursor may have moved since we started reading */
		origin = (unsigned int)file->private_data;
		if (supersize == sizeof(preserved->buf)) {
			int advance = preserved->cursor - origin;
			if (advance < 0)
				advance += sizeof(preserved->buf);
			supersize += advance;
		}
	}

	if (pos < 0 || pos >= supersize)
		goto out;
	if (count > supersize - pos)
		count = supersize - pos;

	offset = origin + pos;
	if (offset > sizeof(preserved->buf))
		offset -= sizeof(preserved->buf);
	limit = sizeof(preserved->buf) - offset;
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
	if (preserved->ksize != 0) {
		error = -ENOSPC;
		goto out;
	}

	if (count > sizeof(preserved->buf)) {
		buf += count - sizeof(preserved->buf);
		count = sizeof(preserved->buf);
	}

	offset = preserved->cursor;
	limit = sizeof(preserved->buf) - offset;
	residue = count;
	error = -EFAULT;

	if (residue > limit) {
		if (copy_from_user(preserved->buf + offset, buf, limit))
			goto out;
		usize = sizeof(preserved->buf);
		offset = 0;
		residue -= limit;
		buf += limit;
	}

	if (copy_from_user(preserved->buf + offset, buf, residue))
		goto out;

	offset += residue;
	if (usize < offset)
		usize = offset;
	if (preserved->usize < usize)
		preserved->usize = usize;
	if (offset == sizeof(preserved->buf))
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

/*
 * For emergency_restart: at the time of a bug, oops or panic.
 */

static void kcrash_append(unsigned int log_size)
{
	int excess = preserved->usize + preserved->ksize +
			log_size - sizeof(preserved->buf);

	if (excess <= 0) {
		/* kcrash fits without losing any utrace */
		preserved->ksize += log_size;
	} else if (excess <= preserved->usize) {
		/* some of utrace was overwritten by kcrash */
		preserved->usize -= excess;
		preserved->ksize += log_size;
	} else {
		/* no utrace left and kcrash is full */
		preserved->usize = 0;
		preserved->ksize = sizeof(preserved->buf);
	}

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
	struct dentry *dir;

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
	}

	return 0;
}
postcore_initcall(preserved_init);
