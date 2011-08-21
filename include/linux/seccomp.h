#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

struct seq_file;

#ifdef CONFIG_SECCOMP

#include <linux/errno.h>
#include <linux/thread_info.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <asm/seccomp.h>

struct seccomp_filters;
/**
 * struct seccomp_struct - the state of a seccomp'ed process
 *
 * @mode:
 *     if this is 1, the process is under standard seccomp rules
 *             is 13, the process is only allowed to make system calls where
 *                    associated filters evaluate successfully.
 * @filters: Metadata for filters if using CONFIG_SECCOMP_FILTER.
 *           @filters assignment and use should always be guarded by
 *           @filters_guard.
 */
struct seccomp_struct {
	int mode;
#ifdef CONFIG_SECCOMP_FILTER
	struct mutex filters_guard;
	struct seccomp_filters *filters;
#endif
};

extern void __secure_computing(int);
static inline void secure_computing(int this_syscall)
{
	if (unlikely(test_thread_flag(TIF_SECCOMP)))
		__secure_computing(this_syscall);
}

extern long prctl_get_seccomp(void);
extern long prctl_set_seccomp(unsigned long);

#else /* CONFIG_SECCOMP */

#include <linux/errno.h>

struct seccomp_struct { };
#define secure_computing(x) do { } while (0)

static inline long prctl_get_seccomp(void)
{
	return -EINVAL;
}

static inline long prctl_set_seccomp(unsigned long arg2)
{
	return -EINVAL;
}

#endif /* CONFIG_SECCOMP */

#ifdef CONFIG_SECCOMP_FILTER

#define seccomp_filter_init_task(_tsk) do { \
	mutex_init(&(_tsk)->seccomp.filters_guard); \
	(_tsk)->seccomp.filters = NULL; \
} while (0);

/* Do nothing unless seccomp filtering is active. If not, the execve boundary
 * can not be cleanly enforced and preset filters may leak across execve calls.
 */
#define seccomp_filter_fork(_tsk, _orig) do { \
	if ((_tsk)->seccomp.mode) { \
		(_tsk)->seccomp.mode = (_orig)->seccomp.mode; \
		mutex_lock(&(_orig)->seccomp.filters_guard); \
		(_tsk)->seccomp.filters = \
			get_seccomp_filters((_orig)->seccomp.filters); \
		mutex_unlock(&(_orig)->seccomp.filters_guard); \
	} \
} while (0);

/* No locking is needed here because the task_struct will
 * have no parallel consumers.
 */
#define seccomp_filter_free_task(_tsk) do { \
	put_seccomp_filters((_tsk)->seccomp.filters); \
} while (0);

extern int seccomp_show_filters(struct seccomp_filters *filters,
				struct seq_file *);
extern long seccomp_set_filter(int, char *);
extern long seccomp_clear_filter(int);
extern long seccomp_get_filter(int, char *, unsigned long);

extern long prctl_set_seccomp_filter(unsigned long, unsigned long,
				     char __user *);
extern long prctl_get_seccomp_filter(unsigned long, unsigned long,
				     char __user *, unsigned long);
extern long prctl_clear_seccomp_filter(unsigned long, unsigned long);

extern struct seccomp_filters *get_seccomp_filters(struct seccomp_filters *);
extern void put_seccomp_filters(struct seccomp_filters *);

extern int seccomp_test_filters(int);
extern void seccomp_filter_log_failure(int);

#else  /* CONFIG_SECCOMP_FILTER */

struct seccomp_filters { };
#define seccomp_filter_init_task(_tsk) do { } while (0);
#define seccomp_filter_fork(_tsk, _orig) do { } while (0);
#define seccomp_filter_free_task(_tsk) do { } while (0);

static inline int seccomp_show_filters(struct seccomp_filters *filters,
				       struct seq_file *m)
{
	return -ENOSYS;
}

static inline long seccomp_set_filter(int syscall_nr, char *filter)
{
	return -ENOSYS;
}

static inline long seccomp_clear_filter(int syscall_nr)
{
	return -ENOSYS;
}

static inline long seccomp_get_filter(int syscall_nr,
				      char *buf, unsigned long available)
{
	return -ENOSYS;
}

static inline long prctl_set_seccomp_filter(unsigned long a2, unsigned long a3,
					    char __user *a4)
{
	return -ENOSYS;
}

static inline long prctl_clear_seccomp_filter(unsigned long a2,
					      unsigned long a3)
{
	return -ENOSYS;
}

static inline long prctl_get_seccomp_filter(unsigned long a2, unsigned long a3,
					    char __user *a4, unsigned long a5)
{
	return -ENOSYS;
}
#endif  /* CONFIG_SECCOMP_FILTER */
#endif /* _LINUX_SECCOMP_H */
