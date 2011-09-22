/*
 * arch/arm/mach-tegra/include/mach/uncompress.h
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *	Doug Anderson <dianders@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_UNCOMPRESS_H
#define __MACH_TEGRA_UNCOMPRESS_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/serial_reg.h>

#include <mach/iomap.h>

u32 uart_base;

#define DEBUG_UART_SHIFT	2

static void putc(int c)
{
	volatile u8 *uart = (volatile u8 *)uart_base;

	if (uart == NULL)
		return;

	while (!(uart[UART_LSR << DEBUG_UART_SHIFT] & UART_LSR_THRE))
		barrier();
	uart[UART_TX << DEBUG_UART_SHIFT] = c;
}

static inline void flush(void)
{
}

/*
 * Setup before decompression.  This is where we do UART selection for
 * earlyprintk and init the uart_base register.
 */
static inline void arch_decomp_setup(void)
{
	static const u32 uarts[] = {
		TEGRA_UARTA_BASE,
		TEGRA_UARTB_BASE,
		TEGRA_UARTC_BASE,
		TEGRA_UARTD_BASE,
		TEGRA_UARTE_BASE,
	};
	const u32 num_uarts = ARRAY_SIZE(uarts);
	u32 i;

	/*
	 * Look for the first UART that has a 'D' in the scratchpad register,
	 * which should be set by the bootloader to tell us which UART to use
	 * for debugging.  If nothing found, we'll fall back to what's
	 * specified in TEGRA_DEBUG_UART_BASE.
	 */
	uart_base = TEGRA_DEBUG_UART_BASE;
	for (i = 0; i < num_uarts; i++) {
		volatile u8 *uart = (volatile u8 *)uarts[i];

		if (uart[UART_SCR << DEBUG_UART_SHIFT] == 'D') {
			uart_base = uarts[i];
			break;
		}
	}
}

static inline void arch_decomp_wdog(void)
{
}

#endif
