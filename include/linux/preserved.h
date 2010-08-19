#ifndef _LINUX_PRESERVED_H
#define _LINUX_PRESERVED_H
/*
 * Declarations of functions external to kernel/preserved.c, for its
 * particular use: in preserving kernel crash messages in RAM across reboot.
 */

/* in kernel/printk.c */
extern unsigned int copy_log_buf(char *buf, unsigned int buf_size,
					    unsigned int cursor);

#endif /* _LINUX_PRESERVED_H */
