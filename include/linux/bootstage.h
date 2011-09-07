#ifndef __BOOTSTAGE_H
#define __BOOTSTAGE_H

#ifdef CONFIG_BOOTSTAGE
unsigned long bootstage_mark(const char *name);
unsigned long bootstage_mark_early(const char *name);
void insert_bootstage(int idx, const char *name, unsigned long time);
#else
static inline unsigned long bootstage_mark(const char *name)
{
	return 0;
}
static inline unsigned long bootstage_mark_early(const char *name)
{
	return 0;
}
#endif

#endif
