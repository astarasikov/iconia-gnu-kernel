/*
 * Copyright (C) 2010 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * This file is released under the GPLv2: see the file COPYING for details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>

/*
 * Flesh out this skeleton in the next commit.
 */

void emergency_restart(void)	/* overriding the __weak one in kernel/sys.c */
{
	machine_emergency_restart();
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
	return 0;
}
postcore_initcall(preserved_init);
