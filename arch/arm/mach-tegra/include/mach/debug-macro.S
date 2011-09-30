/*
 * arch/arm/mach-tegra/include/mach/debug-macro.S
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

#include <linux/serial_reg.h>

#include <asm/memory.h>

#include <mach/io.h>
#include <mach/iomap.h>

#define UART_SHIFT			2
#define TEGRA_DEBUG_UART_OFFSET		(TEGRA_DEBUG_UART_BASE & 0xFFFF)

		/*
		 * Order matters for this section; code below assumes that
		 * phys is 4 from config and virt is 8 from config.
		 *
		 * By default, we'll start the UART by looking at whatever
		 * was specified by CONFIG_TEGRA_DEBUG_UART_XXX, but on the
		 * first use of addruart we'll search all UARTs for one with
		 * a 'D' in the scratchpad register.  If we find one, we'll
		 * use it instead.
		 */
		.pushsection .data
tegra_uart_config:	.word	0
tegra_uart_phys:	.word	IO_APB_PHYS + TEGRA_DEBUG_UART_OFFSET
tegra_uart_virt:	.word	IO_APB_VIRT + TEGRA_DEBUG_UART_OFFSET
		.popsection


		/* Put address of tegra_uart_config into \ra */
		.macro	get_uart_config_addr, ra
		mrc	p15, 0, \ra, c1, c0
		tst	\ra, #1			@ MMU enabled?
		ldreq	\ra, =__virt_to_phys(tegra_uart_config)	@ not enabled
		ldrne	\ra, =tegra_uart_config	@ enabled
		.endm

		/* Check whether a given UART is the debug UART; sets cond. */
		.macro	check_uart, rio, rtmp, uart_base
		orr	\rtmp, \rio, #(\uart_base & 0xFF)
		orr	\rtmp, \rtmp, #(\uart_base & 0xFF00)
		ldrb	\rtmp, [\rtmp, #(UART_SCR << UART_SHIFT)]
		cmp	\rtmp, #'D'
		.endm

		/*
		 * Store the given UART in the data section; needs two
		 * temp registers to work with.
		 */
		.macro	store_uart, rx, ry, uart_base
		get_uart_config_addr \rx
		ldr	\ry, =(IO_APB_PHYS + (\uart_base & 0xFFFF))
		str	\ry, [\rx, #4]
		ldr	\ry, =(IO_APB_VIRT + (\uart_base & 0xFFFF))
		str	\ry, [\rx, #8]
		.endm


		/*
		 * Get phys and virt addr of the debug UART.  Return results in
		 * registers \rp and \rv, which are the only two registers
		 * we get to work with.  Code is loosely based on OMAP
		 * version of debug-macro.S.
		 */
		.macro	addruart, rp, rv

10:		get_uart_config_addr \rp
		ldr	\rv, [\rp, #0]
		cmp	\rv, #1			@ is port configured?
		beq	99f			@ ...yes? jump to end

		mov	\rv, #1 		@ store so next time we are
		str	\rv, [\rp, #0]		@ consider oursevles configured

		/* Use \rp to hold IO base address while we search for 'D' */
		mrc	p15, 0, \rp, c1, c0
		tst	\rp, #1			@ MMU enabled?
		ldreq	\rp, =IO_APB_PHYS	@ MMU not enabled
		ldrne	\rp, =IO_APB_VIRT	@ MMU enabled

		/* Check for UARTA */
		check_uart \rp, \rv, TEGRA_UARTA_BASE
		bne	21f			@ not UARTA
		store_uart \rp, \rv, TEGRA_UARTA_BASE
		b	10b

		/* Check for UARTB */
21:		check_uart \rp, \rv, TEGRA_UARTB_BASE
		bne	22f			@ not UARTB
		store_uart \rp, \rv, TEGRA_UARTB_BASE
		b	10b

		/* Check for UARTC */
22:		check_uart \rp, \rv, TEGRA_UARTC_BASE
		bne	23f			@ not UARTC
		store_uart \rp, \rv, TEGRA_UARTC_BASE
		b	10b

		/* Check for UARTD */
23:		check_uart \rp, \rv, TEGRA_UARTD_BASE
		bne	24f			@ not UARTD
		store_uart \rp, \rv, TEGRA_UARTD_BASE
		b	10b

		/* Check for UARTE */
24:		check_uart \rp, \rv, TEGRA_UARTE_BASE
		bne	10b			@ not UARTE; give up
		store_uart \rp, \rv, TEGRA_UARTE_BASE
		b	10b

		/* When you jump to here \rp has addr of tegra_uart_config */
99:		ldr	\rv, [\rp, #8]		@ virt is 8 bytes after config
		ldr	\rp, [\rp, #4]		@ phys is 4 bytes after config
		.endm

/*
 * Code below is swiped from <asm/hardware/debug-8250.S>, but add an extra
 * check to make sure that we aren't in the CONFIG_TEGRA_DEBUG_UART_NONE case.
 * We use the fact that all 5 valid UARTS addresses all have something in the
 * 2nd-to-lowest byte.
 */
		.macro	senduart,rd,rx
		tst	\rx, #0x0000ff00
		strneb	\rd, [\rx, #UART_TX << UART_SHIFT]
1001:
		.endm

		.macro	busyuart,rd,rx
		tst	\rx, #0x0000ff00
		beq	1002f
1001:		ldrb	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1001b
1002:
		.endm

		.macro	waituart,rd,rx
#ifdef FLOW_CONTROL
		tst	\rx, #0x0000ff00
		beq	1002f
1001:		ldrb	\rd, [\rx, #UART_MSR << UART_SHIFT]
		tst	\rd, #UART_MSR_CTS
		beq	1001b
1002:
#endif
		.endm

