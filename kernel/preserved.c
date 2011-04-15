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
#include <linux/nvram.h>
#include <linux/preserved.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>

/*
 * x86 notes:
 *
 * Much of the complexity here comes from a particular feature of the ChromeOS
 * boot firmware: although it reserves an area of RAM for our use, and that
 * area has been seen to be preserved across ordinary reboot, that can only
 * be guaranteed if we approach reboot from the S3 suspend-to-RAM state.
 *
 * In /sys/devices/platform/chromeos_acpi/CHNV, the ChromeOS ACPI driver
 * reports an offset in /dev/nvram at which a flag can be set before entering
 * S3: to tell the firmware to reboot instead of resume when awakened.
 *
 * The ifdefs below allow this file to be built without all the dependencies
 * which that feature adds.  And even when it is built in, by default we go
 * to a simple reboot, unless the required nvram offset has been written into
 * /sys/kernel/debug/preserved/chnv here.
 */
#if defined(CONFIG_PROC_SYSCTL)	/* for proc_dointvec_minmax() */	&& \
    defined(CONFIG_NVRAM)	/* for nvram_read/write_byte () */	&& \
    defined(CONFIG_RTC_CLASS)	/* for rtc_read/write_time() */		&& \
    defined(CONFIG_ACPI_SLEEP)	/* for acpi_S3_reboot() */		&& \
    defined(CONFIG_SUSPEND)	/* for acpi_S3_reboot() */

#define CHROMEOS_S3_REBOOT

#define NVRAM_BYTES (128 - NVRAM_FIRST_BYTE)	/* from drivers/char/nvram.c */
#define CHNV_DEBUG_RESET_FLAG	0x40		/* magic flag for S3 reboot */
#define AWAKEN_AFTER_SECONDS	2		/* 1 might fire too early?? */

/*
 * ACPI reports offset in NVRAM of CHromeos NVram byte used to program BIOS:
 * that offset is expected to be 94 (0x5e) when it is supported.  We shall
 * rely upon userspace to pass it here from the chromeos_acpi driver;
 * or leave it at -1, in which case a simple reboot works for now.
 */
static int chromeos_nvram_index = -1;

#else /* !CHROMEOS_S3_REBOOT */

#define chromeos_nvram_index	-1

#endif /* CHROMEOS_S3_REBOOT */

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

#ifdef CHROMEOS_S3_REBOOT
/*
 * chnv read and write chromeos_nvram_index like a /proc/sys sysctl value
 * (debugfs builtins are designed for unsigned values without rangechecking).
 */
static int minus_one = -1;
static int nvram_max = NVRAM_BYTES - 1;
static struct ctl_table chnv_ctl = {
	.procname	= "chnv",
	.data		= &chromeos_nvram_index,
	.maxlen		= sizeof(int),
	.mode		= 0644,
	.proc_handler	= &proc_dointvec_minmax,
	.extra1		= &minus_one,
	.extra2		= &nvram_max,
};

static ssize_t chnv_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	return proc_dointvec_minmax(&chnv_ctl, 0,
		(void __user *)buf, &count, ppos) ? : count;
}

static ssize_t chnv_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	return proc_dointvec_minmax(&chnv_ctl, 1,
		(void __user *)buf, &count, ppos) ? : count;
}

static const struct file_operations chnv_operations = {
	.read	= chnv_read,
	.write	= chnv_write,
};

/*
 * For emergency_restart: at the time of a bug, oops or panic.
 */

static int rtc_may_wakeup(struct device *dev, void *data)
{
	struct rtc_device *rtc = to_rtc_device(dev);
	return rtc->ops->set_alarm && device_may_wakeup(rtc->dev.parent);
}

static int set_rtc_alarm(int seconds)
{
	struct device *dev;
	struct rtc_device *rtc;
	struct rtc_wkalrm alarm;
	unsigned long now;
	int error;

	dev = class_find_device(rtc_class, NULL, NULL, rtc_may_wakeup);
	if (!dev)
		return -ENODEV;

	rtc = to_rtc_device(dev);
	error = rtc_read_time(rtc, &alarm.time);
	if (error)
		return error;

	rtc_tm_to_time(&alarm.time, &now);
	rtc_time_to_tm(now + seconds, &alarm.time);
	alarm.enabled = 1;

	return rtc_set_alarm(rtc, &alarm);
}

static void chromeos_S3_reboot(void)
{
	unsigned char chromeos_nvram_flags;
	int error;

	/*
	 * Overly paranoid, but just reboot if chnv has been corrupted.
	 */
	if (chromeos_nvram_index < 0 ||
	    chromeos_nvram_index >= NVRAM_BYTES) {
		printk(KERN_ERR "S3 reboot: chromeos_nvram_index=%d\n",
					    chromeos_nvram_index);
		return;
	}

	/*
	 * Tell the ChromeOS BIOS to use S3 to preserve RAM,
	 * but then to reboot instead of resuming.
	 */
	chromeos_nvram_flags = nvram_read_byte(chromeos_nvram_index);
	if (chromeos_nvram_flags & CHNV_DEBUG_RESET_FLAG) {
		printk(KERN_ERR "S3 reboot: chromeos_nvram_flags=0x%08x\n",
					    chromeos_nvram_flags);
		return;
	}
	chromeos_nvram_flags |= CHNV_DEBUG_RESET_FLAG;
	nvram_write_byte(chromeos_nvram_flags, chromeos_nvram_index);

	/*
	 * Must set an alarm to awaken from S3 to reboot.
	 */
	error = set_rtc_alarm(AWAKEN_AFTER_SECONDS);
	if (error) {
		printk(KERN_ERR "S3 reboot: set_rtc_alarm()=%d\n", error);
		return;
	}

	acpi_S3_reboot();
}
#else /* !CHROMEOS_S3_REBOOT */

static inline void chromeos_S3_reboot(void)
{
}
#endif /* CHROMEOS_S3_REBOOT */

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

void emergency_restart(void)	/* overriding the __weak one in kernel/sys.c */
{
	/*
	 * Initialize a good header if that's not already been done.
	 */
	if (preserved_is_valid() || preserved_make_valid()) {
		printk(KERN_INFO "Preserving kcrash across %sreboot\n",
			(chromeos_nvram_index == -1) ? "" : "S3 ");

		/*
		 * Copy printk's log_buf (kmsg or dmesg) into our preserved buf,
		 * perhaps appending to a kcrash from the previous boot.
		 */
		kcrash_preserve(true);

		/* on x86, slip into S3 then reboot */
		if (chromeos_nvram_index != -1) {
			chromeos_S3_reboot();
			/*
			 * It's an error if we reach here, so rewrite the log.
			 */
			kcrash_preserve(false);
		}
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
#ifdef CHROMEOS_S3_REBOOT
			debugfs_create_file("chnv", S_IFREG|S_IRUGO|S_IWUSR,
						dir, NULL, &chnv_operations);
#endif
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
