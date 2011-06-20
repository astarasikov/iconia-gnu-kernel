#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/ktime.h>

#include "linux/bootstage.h"

#define BOOTSTAGE_COUNT 8
#define MAX_NAME 32
struct bootstage_record {
	unsigned long time;
	const char name[MAX_NAME];
};

static DEFINE_MUTEX(bootstage_mutex);

/*
 * this array is to record the bootstages at the beginning of
 * kernel initialization, before memory is initialized.
 */
static struct bootstage_record bootstages[BOOTSTAGE_COUNT];

/* the number of timing recorded in the bootstages array */
static int num_bootstages;
/* the space size of the bootstages array */
static int cap_bootstages = BOOTSTAGE_COUNT;
static struct bootstage_record *full_bootstages = bootstages;

static unsigned long __bootstage_mark_early(int idx, const char *name)
{
	unsigned long curr = timer_get_us();

	strlcpy(full_bootstages[idx].name, name, MAX_NAME);

	/* this records timing in microseconds. */
	full_bootstages[idx].time = curr;
	return full_bootstages[idx].time;
}

static unsigned long __bootstage_mark(int idx, const char *name)
{
	struct timespec ts;

	/* this function can be used only after timekeeping is called. */
	ktime_get_ts(&ts);
	strlcpy(full_bootstages[idx].name, name, MAX_NAME);

	/* this records timing in microseconds. */
	full_bootstages[idx].time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	return full_bootstages[idx].time;
}

static inline int __inc_bootstages(void)
{
	struct bootstage_record *tmp;

	cap_bootstages *= 2;
	tmp = kmalloc(sizeof(*tmp) * cap_bootstages, GFP_KERNEL);
	if (!tmp)
		return -1;
	memcpy(tmp, full_bootstages, sizeof(*tmp) * num_bootstages);

	/*
	 * full_bootstages first points to bootstages,
	 * which is a static array.
	 */
	if (full_bootstages != bootstages)
		kfree(full_bootstages);
	full_bootstages = tmp;
	return 0;
}

/*
 * This is used during the initialization of the kernel.
 */
unsigned long bootstage_mark(const char *name)
{
	unsigned long ret;

	mutex_lock(&bootstage_mutex);

	if (num_bootstages == cap_bootstages) {
		if (__inc_bootstages() < 0) {
			mutex_unlock(&bootstage_mutex);
			return -1;
		}
	}

	ret = __bootstage_mark(num_bootstages, name);
	num_bootstages++;

	mutex_unlock(&bootstage_mutex);
	return ret;
}

/*
 * This is the same as bootstage_mark, but it can be used
 * before memory and even timekeeping are initialized.
 */
unsigned long bootstage_mark_early(const char *name)
{
	unsigned long ret;

	mutex_lock(&bootstage_mutex);

	if (num_bootstages >= cap_bootstages)
		return -1;

	ret = __bootstage_mark_early(num_bootstages, name);
	num_bootstages++;

	mutex_unlock(&bootstage_mutex);
	return ret;
}

static int get_bootstage_text(char *buf, int size)
{
	int i;
	int written = 0;

	for (i = 0; i < num_bootstages; i++) {
		int ret = scnprintf(buf + written, size - written, "%s\t%ld\n",
				full_bootstages[i].name,
				full_bootstages[i].time);
		written += ret;
		if (written == size && i < num_bootstages - 1) {
			printk(KERN_WARNING "bootstages array is too large");
			break;
		}
	}
	return written;
}

static ssize_t bootstage_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	int err = 0;
	char *lbuf;
	int written = 0;
	int size = (MAX_NAME + 10) * num_bootstages;

	/*
	 * If the user tries to continue reading,
	 * return 0 to notify the user all content has been read.
	 */
	if (*ppos)
		return 0;

	lbuf = kmalloc(size, GFP_KERNEL);
	if (!lbuf)
		return -ENOMEM;

	mutex_lock(&bootstage_mutex);
	err = -EFAULT;
	written = get_bootstage_text(lbuf, size);
	if (count > written)
		count = written;
	if (!copy_to_user(buf, lbuf, count)) {
		err = count;
		*ppos += count;
	}

	mutex_unlock(&bootstage_mutex);
	kfree(lbuf);
	return err;
}

static ssize_t bootstage_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char *lbuf;
	int err = 0;

	lbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!lbuf)
		return -ENOMEM;
	if (copy_from_user(lbuf, buf, count)) {
		kfree(lbuf);
		return -EFAULT;
	}

	/* The input string might end with \n or not end with 0. */
	if (lbuf[count - 1] == '\n')
		lbuf[count - 1] = '\0';
	else
		lbuf[count] = '\0';

	mutex_lock(&bootstage_mutex);
	if (num_bootstages == cap_bootstages) {
		if (__inc_bootstages() < 0)
			err = -EFAULT;
	}
	if (err == 0) {
		__bootstage_mark(num_bootstages, lbuf);
		num_bootstages++;
		err = count;
	}
	mutex_unlock(&bootstage_mutex);
	kfree(lbuf);
	return err;
}

static const struct file_operations report_operations = {
	.read	= bootstage_read,
};

static const struct file_operations mark_operations = {
	.write	= bootstage_write,
};

static int __init bootstage_init(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("bootstage", NULL);
	if (dir && !IS_ERR(dir)) {
		debugfs_create_file("report", S_IFREG|S_IRUSR|S_IRGRP|S_IROTH,
				dir, NULL, &report_operations);
		debugfs_create_file("mark", S_IFREG|S_IWUSR,
				dir, NULL, &mark_operations);
	}
	return 0;
}

postcore_initcall(bootstage_init);
