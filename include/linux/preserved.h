#ifndef _LINUX_PRESERVED_H
#define _LINUX_PRESERVED_H
/*
 * Declarations of functions external to kernel/preserved.c, for its
 * particular use: in preserving kernel crash messages in RAM across reboot.
 */

/* in kernel/printk.c */
extern unsigned int copy_log_buf(char *buf, unsigned int buf_size,
					    unsigned int cursor);
/* in drivers/acpi/sleep.c */
extern void acpi_S3_reboot(void);

#endif /* _LINUX_PRESERVED_H */
