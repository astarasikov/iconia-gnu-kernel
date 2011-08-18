/* filter engine-based seccomp system call filtering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2011 The Chromium OS Authors <chromium-os-dev@chromium.org>
 */

#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/ftrace_event.h>
#include <linux/kallsyms.h>
#include <linux/kref.h>
#include <linux/perf_event.h>
#include <linux/prctl.h>
#include <linux/seccomp.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/btree.h>

#include <trace/syscall.h>

/* Pull in *just* to access event_filter->filter_string. */
#include "trace/trace.h"

#define SECCOMP_MAX_FILTER_LENGTH MAX_FILTER_STR_VAL
#define SECCOMP_FILTER_ALLOW "1"
#define SECCOMP_MAX_FILTER_COUNT 65535

/* In the allow-all case for any filter, use an PTR_ERR instead of allocating
 * and evaluating a complete event_filter.
 */
#define IS_ALLOW_FILTER(_filter) \
	(IS_ERR(_filter) && PTR_ERR(_filter) == -ENOENT)
#define ALLOW_FILTER ERR_PTR(-ENOENT)

/**
 * struct seccomp_filters - container for seccomp filters
 *
 * @usage: reference count to manage the object lifetime.
 *         get/put helpers should be used when accessing an instance
 *         outside of a lifetime-guarded section.  In general, this
 *         is only needed for handling shared filters across tasks.
 * @count: size of @event_filters
 * @filter: tree of pointers to seccomp filters
 *
 * seccomp_filters objects should never be modified after being attached
 * to a task_struct.
 */
struct seccomp_filters {
	struct kref usage;
	struct {
		uint32_t compat:1,
			 __reserved:31;
	} flags;
	uint16_t count;
	struct btree_head32 tree;
};

/*
 * Make ftrace support optional
 */

#if defined(CONFIG_FTRACE_SYSCALLS) && defined(CONFIG_PERF_EVENTS)
#include <asm/syscall.h>

/* Make sure we can get to the syscall metadata that ftrace hordes. */
extern struct syscall_metadata *__start_syscalls_metadata[];
extern struct syscall_metadata *__stop_syscalls_metadata[];

static struct syscall_metadata **syscalls_metadata;
static struct syscall_metadata *syscall_nr_to_meta(int nr)
{
	if (!syscalls_metadata || nr >= NR_syscalls || nr < 0)
		return NULL;

	return syscalls_metadata[nr];
}

/* Fix up buffer creation to allow the ftrace filter engine to
 * work with seccomp filter events.
 * Lifted from kernel/trace/trace_syscalls.c
 */

#ifndef ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym, const char *name)
{
	/*
	 * Only compare after the "sys" prefix. Archs that use
	 * syscall wrappers may have syscalls symbols aliases prefixed
	 * with "SyS" instead of "sys", leading to an unwanted
	 * mismatch.
	 */
	return !strcmp(sym + 3, name + 3);
}
#endif

static __init struct syscall_metadata *
find_syscall_meta(unsigned long syscall)
{
	struct syscall_metadata **start;
	struct syscall_metadata **stop;
	char str[KSYM_SYMBOL_LEN];


	start = __start_syscalls_metadata;
	stop = __stop_syscalls_metadata;
	kallsyms_lookup(syscall, NULL, NULL, NULL, str);

	if (arch_syscall_match_sym_name(str, "sys_ni_syscall"))
		return NULL;

	for ( ; start < stop; start++) {
		if ((*start)->name && arch_syscall_match_sym_name(str, (*start)->name))
			return *start;
	}
	return NULL;
}

int __init init_seccomp_filter(void)
{
	struct syscall_metadata *meta;
	unsigned long addr;
	int i;

	syscalls_metadata = kzalloc(sizeof(*syscalls_metadata) *
					NR_syscalls, GFP_KERNEL);
	if (!syscalls_metadata) {
		WARN_ON(1);
		return -ENOMEM;
	}

	for (i = 0; i < NR_syscalls; i++) {
		addr = arch_syscall_addr(i);
		meta = find_syscall_meta(addr);
		if (!meta)
			continue;

		meta->syscall_nr = i;
		syscalls_metadata[i] = meta;
	}

	return 0;
}
core_initcall(init_seccomp_filter);

/*
 * End of the ftrace metadata access fixup.
 */


static int create_event_filter(struct event_filter **filter,
			       int event_type,
			       char *filter_string)
{
	int ret = -ENOSYS;
	struct perf_event event;
	event.filter = NULL;
	ret = ftrace_profile_set_filter(&event, event_type, filter_string);
	if (ret)
		goto out;
	*filter = event.filter;
	(*filter)->filter_string = kstrdup(filter_string, GFP_KERNEL);
	if (!(*filter)->filter_string) {
		ftrace_profile_free_filter(&event);
		ret = -ENOMEM;
	}
out:
	return ret;
}

static char *get_filter_string(struct event_filter *filter)
{
	if (filter)
		return filter->filter_string;
	return NULL;
}

static void free_event_filter(struct event_filter *filter)
{
	struct perf_event event;
	if (IS_ERR_OR_NULL(filter) || IS_ALLOW_FILTER(filter))
		return;
	event.filter = filter;
	ftrace_profile_free_filter(&event);
}

/* ftrace_syscall_enter_state_size - returns the state size required.
 *
 * @nb_args: number of system call args expected.
 *           a negative value implies the maximum allowed.
 */
static size_t ftrace_syscall_enter_state_size(int nb_args)
{
	/* syscall_get_arguments only supports up to 6 arguments. */
	int arg_count = (nb_args >= 0 ? nb_args : 6);
	size_t size = (sizeof(unsigned long) * arg_count) +
		      sizeof(struct syscall_trace_enter);
	size = ALIGN(size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);
	return size;
}

#if 0  /* If we need exit interception. */
static size_t ftrace_syscall_exit_state_size(void)
{
	return ALIGN(sizeof(struct syscall_trace_exit) + sizeof(u32),
		     sizeof(u64)) - sizeof(u32);
}
#endif

/* ftrace_syscall_enter_state - build state for filter matching
 *
 * @buf: buffer to populate with current task state for matching
 * @available: size available for use in the buffer.
 * @entry: optional pointer to the trace_entry member of the state.
 *
 * Returns 0 on success and non-zero otherwise.
 * If @entry is NULL, it will be ignored.
 */
static int ftrace_syscall_enter_state(u8 *buf, size_t available,
				      struct trace_entry **entry)
{
	struct syscall_trace_enter *sys_enter;
	struct syscall_metadata *sys_data;
	int size;
	int syscall_nr;
	struct pt_regs *regs = task_pt_regs(current);

	syscall_nr = syscall_get_nr(current, regs);
	if (syscall_nr < 0)
		return -EINVAL;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return -EINVAL;

	/* Determine the actual size needed. */
	size = ftrace_syscall_enter_state_size(sys_data->nb_args);
	BUG_ON(size > available);
	sys_enter = (struct syscall_trace_enter *)buf;

	/* Populating the struct trace_sys_enter is left to the caller, but
	 * a pointer is returned to encourage opacity.
	 */
	if (entry)
		*entry = &sys_enter->ent;

	sys_enter->nr = syscall_nr;
	syscall_get_arguments(current, regs, 0, sys_data->nb_args,
			      sys_enter->args);
	return 0;
}

/* Encodes translation from sys_enter events to system call numbers.
 * Returns -ENOSYS when the event doesn't match a system call or if
 * current is_compat_task().  ftrace has no awareness of CONFIG_COMPAT
 * yet.
 */
static int event_to_syscall_nr(int event_id)
{
	int nr, nosys = 1;
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		return -ENOSYS;
#endif
	for (nr = 0; nr < NR_syscalls; ++nr) {
		struct syscall_metadata *data = syscall_nr_to_meta(nr);
		if (!data)
			continue;
		nosys = 0;
		if (data->enter_event->event.type == event_id)
			return nr;
	}
	if (nosys)
		return -ENOSYS;
	return -EINVAL;
}


#else  /*  defined(CONFIG_FTRACE_SYSCALLS) && defined(CONFIG_PERF_EVENTS) */

#define create_event_filter(_ef_pptr, _event_type, _str) (-ENOSYS)
#define event_to_syscall_nr(x) (-ENOSYS)
#define syscall_nr_to_meta(x) (NULL)

static void free_event_filter(struct event_filter *filter) { }
static char *get_filter_string(struct event_filter *filter) { return NULL; }

#endif

/**
 * seccomp_filters_alloc - allocates a new filters object
 *
 * Returns ERR_PTR on error or an allocated object.
 */
static struct seccomp_filters *seccomp_filters_alloc(void)
{
	struct seccomp_filters *f;

	f = kzalloc(sizeof(struct seccomp_filters), GFP_KERNEL);
	if (!f)
		return ERR_PTR(-ENOMEM);
	kref_init(&f->usage);
	if (btree_init32(&f->tree))
		return ERR_PTR(-ENOMEM);

	return f;
}

/**
 * seccomp_filters_free - cleans up the filter list and frees the table
 * @filters: NULL or live object to be completely destructed.
 */
static void seccomp_filters_free(struct seccomp_filters *filters)
{
	int nr = 0;
	struct event_filter *ef;
	if (!filters)
		return;
	btree_for_each_safe32(&filters->tree, nr, ef) {
		btree_remove32(&filters->tree, nr);
		free_event_filter(ef);
	}
	btree_destroy32(&filters->tree);
	kfree(filters);
}

static void __put_seccomp_filters(struct kref *kref)
{
	struct seccomp_filters *orig =
		container_of(kref, struct seccomp_filters, usage);
	seccomp_filters_free(orig);
}

static struct event_filter *alloc_event_filter(int syscall_nr,
					       char *filter_string)
{
	struct syscall_metadata *data;
	struct event_filter *filter = NULL;
	int err;

	data = syscall_nr_to_meta(syscall_nr);
	/* Argument-based filtering only works on ftrace-hooked syscalls. */
	err = -ENOSYS;
	if (!data)
		goto fail;
	err = create_event_filter(&filter,
				  data->enter_event->event.type,
				  filter_string);
	if (err)
		goto fail;

	return filter;
fail:
	kfree(filter);
	return ERR_PTR(err);
}

static void seccomp_filters_drop(struct seccomp_filters *filters, int nr)
{
	struct event_filter *filter = btree_remove32(&filters->tree, nr);
	free_event_filter(filter);
}

static void seccomp_filters_drop_exec(struct seccomp_filters *filters)
{
	int nr = __NR_execve;
	if (has_capability_noaudit(current, CAP_SYS_ADMIN))
		return;
#ifdef CONFIG_COMPAT
	if (filters->flags.compat)
		nr = __NR_seccomp_execve_32;
#endif
	seccomp_filters_drop(filters, nr);
}

/**
 * seccomp_filters_copy - copies filters from src to dst.
 *
 * @dst: seccomp_filters to populate.
 * @src: table to read from.
 * @skip: specifies an entry, by system call, to skip.
 *
 * Returns non-zero on failure.
 * Both the source and the destination should have no simultaneous
 * writers, and dst should be exclusive to the caller.
 * If @skip is < 0, it is ignored.
 */
static int seccomp_filters_copy(struct seccomp_filters *dst,
				struct seccomp_filters *src)
{
	int ret = 0, nr;
	struct event_filter *ef;
	memcpy(&dst->flags, &src->flags, sizeof(src->flags));

	btree_for_each_safe32(&src->tree, nr, ef) {
		struct event_filter *filter = ALLOW_FILTER;
		if (!IS_ALLOW_FILTER(ef)) {
			filter = alloc_event_filter(nr, get_filter_string(ef));
			if (IS_ERR(filter)) {
				ret = PTR_ERR(filter);
				goto done;
			}
		}
		if (btree_insert32(&dst->tree, nr, filter,
				   GFP_KERNEL)) {
			ret = -ENOMEM;
			free_event_filter(filter);
			goto done;
		}
		dst->count++;
	}

done:
	return ret;
}

/**
 * seccomp_extend_filter - appends more text to a syscall_nr's filter
 * @filters: unattached filter object to operate on
 * @syscall_nr: syscall number to update filters for
 * @filter_string: string to append to the existing filter
 *
 * The new string will be &&'d to the original filter string to ensure that it
 * always matches the existing predicates or less:
 *   (old_filter) && @filter_string
 * A new seccomp_filters instance is returned on success and a ERR_PTR on
 * failure.
 */
static int seccomp_extend_filter(struct seccomp_filters *filters,
				 int syscall_nr, char *filter_string)
{
	struct event_filter *filter = btree_lookup32(&filters->tree,
						     syscall_nr);
	char *merged = NULL;
	int ret = -EINVAL, expected;

	/* No extending with a "1". */
	if (!strcmp(SECCOMP_FILTER_ALLOW, filter_string))
		goto out;

	/* ftrace events are not aware of CONFIG_COMPAT system calls and will
	 * use the incorrect argument metadata if enabled.
	 */
	ret = -ENOSYS;
	if (filters->flags.compat)
		goto out;

	/* If there is no entry, then there's nothing to extend. */
	ret = -ENOENT;
	if (!filter)
		goto out;

	merged = filter_string;
	if (!IS_ALLOW_FILTER(filter)) {
		merged = kzalloc(SECCOMP_MAX_FILTER_LENGTH + 1, GFP_KERNEL);
		ret = -ENOMEM;
		if (!merged)
			goto out;

		/* Encapsulate the filter strings in parentheses to isolate
		 * operator precedence behavior.
		 */
		expected = snprintf(merged, SECCOMP_MAX_FILTER_LENGTH,
				    "(%s) && (%s)", get_filter_string(filter),
				    filter_string);
		ret = -E2BIG;
		if (expected >= SECCOMP_MAX_FILTER_LENGTH || expected < 0)
			goto out;
	}

	/* Drop the original */
	btree_remove32(&filters->tree, syscall_nr);
	/* Free the old filter */
	free_event_filter(filter);

	/* Replace it */
	filter = alloc_event_filter(syscall_nr, merged);
	if (IS_ERR(filter)) {
		ret = PTR_ERR(filter);
		goto out;
	}
	ret = btree_insert32(&filters->tree, syscall_nr, filter, GFP_KERNEL);
	if (ret)
		free_event_filter(filter);

	/* If insertion failed, then the entry will be dropped completely. */
out:
	if (merged != filter_string)
		kfree(merged);
	return ret;
}

/**
 * seccomp_add_filter - adds a filter for an unfiltered syscall
 * @filters: filters object to add a filter/action to
 * @syscall_nr: system call number to add a filter for
 * @filter_string: the filter string to apply
 *
 * Returns 0 on success and non-zero otherwise.
 */
static int seccomp_add_filter(struct seccomp_filters *filters, int syscall_nr,
			      char *filter_string)
{
	struct event_filter *filter = NULL;
	int ret = 0;

	if (filters->count == SECCOMP_MAX_FILTER_COUNT)
		return -ENOSPC;

	if (!strcmp(SECCOMP_FILTER_ALLOW, filter_string)) {
		/* For unrestricted allow rules, insert a placeholder instead
		 * of allocating an actual event_filter.
		 */
		ret = btree_insert32(&filters->tree, syscall_nr,
				     ALLOW_FILTER, GFP_KERNEL);
		goto out;
	}

	/* ftrace events are not aware of CONFIG_COMPAT system calls and will
	 * use the incorrect argument metadata if enabled.
	 */
	ret = -ENOSYS;
	if (filters->flags.compat)
		goto out;

	ret = 0;
	filter = alloc_event_filter(syscall_nr, filter_string);
	if (IS_ERR(filter)) {
		ret = PTR_ERR(filter);
		goto out;
	}
	/* Always add to the last slot available since additions are
	 * are only done one at a time.
	 */
	ret = btree_insert32(&filters->tree, syscall_nr, filter, GFP_KERNEL);
out:
	if (ret)
		free_event_filter(filter);
	else
		filters->count++;
	return ret;
}

/* Wrap optional ftrace syscall support. Returns 1 on match or 0 otherwise. */
static int filter_match_current(struct event_filter *event_filter)
{
	int err = 0;
#ifdef CONFIG_FTRACE_SYSCALLS
	uint8_t syscall_state[64];

	memset(syscall_state, 0, sizeof(syscall_state));

	/* The generic tracing entry can remain zeroed. */
	err = ftrace_syscall_enter_state(syscall_state, sizeof(syscall_state),
					 NULL);
	if (err)
		return 0;

	err = filter_match_preds(event_filter, syscall_state);
#endif
	return err;
}

static const char *syscall_nr_to_name(int syscall)
{
	const char *syscall_name = "unknown";
	struct syscall_metadata *data = syscall_nr_to_meta(syscall);
	if (data)
		syscall_name = data->name;
	return syscall_name;
}

static void filters_set_compat(struct seccomp_filters *filters)
{
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		filters->flags.compat = 1;
#endif
}

static inline int filters_compat_mismatch(struct seccomp_filters *filters)
{
	int ret = 0;
	if (!filters)
		return 0;
#ifdef CONFIG_COMPAT
	if (!!(is_compat_task()) != filters->flags.compat)
		ret = 1;
#endif
	return ret;
}

static inline int syscall_is_execve(int syscall)
{
	int nr = __NR_execve;
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		nr = __NR_seccomp_execve_32;
#endif
	return syscall == nr;
}

#ifndef KSTK_EIP
#define KSTK_EIP(x) 0L
#endif

void seccomp_filter_log_failure(int syscall)
{
	pr_info("%s[%d]: system call %d (%s) blocked at 0x%lx\n",
		current->comm, task_pid_nr(current), syscall,
		syscall_nr_to_name(syscall), KSTK_EIP(current));
}

/* put_seccomp_filters - decrements the ref count of @orig and may free. */
void put_seccomp_filters(struct seccomp_filters *orig)
{
	if (!orig)
		return;
	kref_put(&orig->usage, __put_seccomp_filters);
}

/* get_seccomp_filters - increments the reference count of @orig. */
struct seccomp_filters *get_seccomp_filters(struct seccomp_filters *orig)
{
	if (!orig)
		return NULL;
	/* XXX: kref needs overflow prevention support. */
	kref_get(&orig->usage);
	return orig;
}

/**
 * seccomp_test_filters - tests 'current' against the given syscall
 * @syscall: number of the system call to test
 *
 * Returns 0 on ok and non-zero on error/failure.
 */
int seccomp_test_filters(int syscall)
{
	struct event_filter *filter;
	struct seccomp_filters *filters;
	int ret = -EACCES;

	mutex_lock(&current->seccomp.filters_guard);
	/* No reference counting is done. filters_guard should protect the
	 * lifetime of any existing pointer below.
	 */
	filters = current->seccomp.filters;
	if (!filters)
		goto out;

	if (filters_compat_mismatch(filters)) {
		pr_info("%s[%d]: seccomp_filter compat() mismatch.\n",
			current->comm, task_pid_nr(current));
		goto out;
	}

	filter = btree_lookup32(&filters->tree, syscall);
	if (!filter)
		goto out;

	ret = 0;
	if (IS_ALLOW_FILTER(filter))
		goto out;

	if (!filter_match_current(filter))
		ret = -EACCES;
out:
	mutex_unlock(&current->seccomp.filters_guard);
	return ret;
}

/**
 * seccomp_show_filters - prints the current filter state to a seq_file
 * @filters: properly get()'d filters object
 * @m: the prepared seq_file to receive the data
 *
 * Returns 0 on a successful write.
 */
int seccomp_show_filters(struct seccomp_filters *filters, struct seq_file *m)
{
	int nr;
	struct event_filter *ef;
	if (!filters)
		goto out;

	btree_for_each_safe32(&filters->tree, nr, ef) {
		const char *filter_string = SECCOMP_FILTER_ALLOW;
		seq_printf(m, "%d (%s): ", nr, syscall_nr_to_name(nr));
		if (!IS_ALLOW_FILTER(ef))
			filter_string = get_filter_string(ef);
		seq_printf(m, "%s\n", filter_string);
	}
out:
	return 0;
}
EXPORT_SYMBOL_GPL(seccomp_show_filters);

/**
 * seccomp_get_filter - copies the filter_string into "buf"
 * @syscall_nr: system call number to look up
 * @buf: destination buffer
 * @bufsize: available space in the buffer.
 *
 * Context: User context only. This function may sleep on allocation and
 *          operates on current. current must be attempting a system call
 *          when this is called.
 *
 * Looks up the filter for the given system call number on current.  If found,
 * the string length of the NUL-terminated buffer is returned and < 0 is
 * returned on error. The NUL byte is not included in the length.
 */
long seccomp_get_filter(int syscall_nr, char *buf, unsigned long bufsize)
{
	struct seccomp_filters *filters;
	struct event_filter *filter;
	long ret = -EINVAL;

	if (bufsize > SECCOMP_MAX_FILTER_LENGTH)
		bufsize = SECCOMP_MAX_FILTER_LENGTH;

	mutex_lock(&current->seccomp.filters_guard);
	filters = current->seccomp.filters;

	if (!filters)
		goto out;

	ret = -ENOENT;
	filter = btree_lookup32(&filters->tree, syscall_nr);
	if (!filter)
		goto out;

	if (IS_ALLOW_FILTER(filter)) {
		ret = strlcpy(buf, SECCOMP_FILTER_ALLOW, bufsize);
		goto copied;
	}

	ret = strlcpy(buf, get_filter_string(filter), bufsize);

copied:
	if (ret >= bufsize) {
		ret = -ENOSPC;
		goto out;
	}
out:
	mutex_unlock(&current->seccomp.filters_guard);
	return ret;
}
EXPORT_SYMBOL_GPL(seccomp_get_filter);

/**
 * seccomp_clear_filter: clears the seccomp filter for a syscall.
 * @syscall_nr: the system call number to clear filters for.
 *
 * Context: User context only. This function may sleep on allocation and
 *          operates on current. current must be attempting a system call
 *          when this is called.
 *
 * Returns 0 on success.
 */
long seccomp_clear_filter(int syscall_nr)
{
	struct seccomp_filters *filters = NULL, *orig_filters;
	int ret = -EINVAL;

	mutex_lock(&current->seccomp.filters_guard);
	orig_filters = current->seccomp.filters;

	if (!orig_filters)
		goto out;

	if (filters_compat_mismatch(orig_filters))
		goto out;

	/* Bail if the entry doesn't exist. */
	if (!btree_lookup32(&orig_filters->tree, syscall_nr))
		goto out;

	/* Create a new filters object for the task */
	filters = seccomp_filters_alloc();
	if (IS_ERR(filters)) {
		ret = PTR_ERR(filters);
		goto out;
	}

	/* Copy, but drop the requested entry. */
	ret = seccomp_filters_copy(filters, orig_filters);
	if (ret)
		goto out;
	seccomp_filters_drop(filters, syscall_nr);
	seccomp_filters_drop_exec(filters);
	get_seccomp_filters(filters);  /* simplify the out: path */

	current->seccomp.filters = filters;
	put_seccomp_filters(orig_filters);  /* for the task */
out:
	put_seccomp_filters(filters);  /* for the extra get */
	mutex_unlock(&current->seccomp.filters_guard);
	return ret;
}
EXPORT_SYMBOL_GPL(seccomp_clear_filter);

/**
 * seccomp_set_filter: - Adds/extends a seccomp filter for a syscall.
 * @syscall_nr: system call number to apply the filter to.
 * @filter: ftrace filter string to apply.
 *
 * Context: User context only. This function may sleep on allocation and
 *          operates on current. current must be attempting a system call
 *          when this is called.
 *
 * New filters may be added for system calls when the current task is
 * not in a secure computing mode (seccomp).  Otherwise, existing filters may
 * be extended.
 *
 * Returns 0 on success or an errno on failure.
 */
long seccomp_set_filter(int syscall_nr, char *filter)
{
	struct seccomp_filters *filters = NULL, *orig_filters = NULL;
	struct event_filter *ef = NULL;
	long ret = -EPERM;

	/* execve is only allowed for privileged processes. */
	if (!capable(CAP_SYS_ADMIN) && syscall_is_execve(syscall_nr))
		goto out;

	mutex_lock(&current->seccomp.filters_guard);
	ret = -EINVAL;
	if (!filter)
		goto out;

	filter = strstrip(filter);
	/* Disallow empty strings. */
	if (filter[0] == 0)
		goto out;

	orig_filters = current->seccomp.filters;

	/* After the first call, compatibility mode is selected permanently. */
	ret = -EACCES;
	if (filters_compat_mismatch(orig_filters))
		goto out;

	if (orig_filters)
		ef = btree_lookup32(&orig_filters->tree, syscall_nr);

	if (!ef) {
		/* Don't allow DENYs to be changed when in a seccomp mode */
		ret = -EACCES;
		if (current->seccomp.mode)
			goto out;
	}

	filters = seccomp_filters_alloc();
	if (IS_ERR(filters)) {
		ret = PTR_ERR(filters);
		goto out;
	}

	filters_set_compat(filters);
	if (orig_filters) {
		ret = seccomp_filters_copy(filters, orig_filters);
		if (ret)
			goto out;
		seccomp_filters_drop_exec(filters);
	}

	if (!ef)
		ret = seccomp_add_filter(filters, syscall_nr, filter);
	else
		ret = seccomp_extend_filter(filters, syscall_nr, filter);

	if (ret)
		goto out;
	get_seccomp_filters(filters);  /* simplify the error paths */

	current->seccomp.filters = filters;
	put_seccomp_filters(orig_filters);  /* for the task */
out:
	put_seccomp_filters(filters);  /* for get or task, on err */
	mutex_unlock(&current->seccomp.filters_guard);
	return ret;
}
EXPORT_SYMBOL_GPL(seccomp_set_filter);

long prctl_set_seccomp_filter(unsigned long id_type,
			      unsigned long id,
			      char __user *user_filter)
{
	int nr;
	long ret;
	char *filter = NULL;

	ret = -EINVAL;
	if (id_type != PR_SECCOMP_FILTER_EVENT &&
	    id_type != PR_SECCOMP_FILTER_SYSCALL)
		goto out;

	if (id > (unsigned long) INT_MAX)
		goto out;
	nr = (int) id;

	if (id_type == PR_SECCOMP_FILTER_EVENT)
		nr = event_to_syscall_nr(nr);

	if (nr < 0) {
		ret = nr;
		goto out;
	}

	ret = -EFAULT;
	if (!user_filter)
		goto out;

	filter = kzalloc(SECCOMP_MAX_FILTER_LENGTH + 1, GFP_KERNEL);
	ret = -ENOMEM;
	if (!filter)
		goto out;

	ret = -EFAULT;
	if (strncpy_from_user(filter, user_filter,
			      SECCOMP_MAX_FILTER_LENGTH - 1) < 0)
		goto out;

	ret = seccomp_set_filter(nr, filter);

out:
	kfree(filter);
	return ret;
}

long prctl_clear_seccomp_filter(unsigned long id_type, unsigned long id)
{
	int nr = -1;
	long ret = -EINVAL;

	if (id_type != PR_SECCOMP_FILTER_EVENT &&
	    id_type != PR_SECCOMP_FILTER_SYSCALL)
		goto out;

	if (id > (unsigned long) INT_MAX)
		goto out;

	nr = (int) id;
	if (id_type == PR_SECCOMP_FILTER_EVENT)
		nr = event_to_syscall_nr(nr);

	if (nr < 0) {
		ret = nr;
		goto out;
	}

	ret = seccomp_clear_filter(nr);
out:
	return ret;
}

long prctl_get_seccomp_filter(unsigned long id_type, unsigned long id,
			      char __user *dst, unsigned long available)
{
	unsigned long copied;
	char *buf = NULL;
	int nr, ret = -EINVAL;

	if (!available)
		goto out;

	/* Ignore extra buffer space. */
	if (available > SECCOMP_MAX_FILTER_LENGTH)
		available = SECCOMP_MAX_FILTER_LENGTH;

	if (id_type != PR_SECCOMP_FILTER_EVENT &&
	    id_type != PR_SECCOMP_FILTER_SYSCALL)
		goto out;

	if (id > (unsigned long) INT_MAX)
		goto out;
	nr = (int) id;

	if (id_type == PR_SECCOMP_FILTER_EVENT)
		nr = event_to_syscall_nr(nr);

	if (nr < 0) {
		ret = nr;
		goto out;
	}

	ret = -ENOMEM;
	buf = kzalloc(available, GFP_KERNEL);
	if (!buf)
		goto out;

	ret = seccomp_get_filter(nr, buf, available);
	if (ret < 0)
		goto out;

	/* Include the NUL byte in the copy. */
	copied = copy_to_user(dst, buf, ret + 1);
	ret = -ENOSPC;
	if (copied)
		goto out;
	ret = 0;
out:
	kfree(buf);
	return ret;
}
