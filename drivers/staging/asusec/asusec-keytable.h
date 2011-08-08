/*
 * Keyboard class input driver for keyboards connected to an NvEc compliant
 * embedded controller
 *
 * Copyright (c) 2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

static unsigned short code_tab_102us[] = {
	KEY_GRAVE,	/* 0x00 */
	KEY_ESC,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_0,
	KEY_MINUS,
	KEY_TAB,
	KEY_GRAVE,
	KEY_TAB,
	KEY_Q,		/* 0x10 */
	KEY_RIGHTALT,
	KEY_LEFTSHIFT,
	KEY_R,
	KEY_RIGHTCTRL,
	KEY_Q,
	KEY_1,
	KEY_I,
	KEY_O,
	KEY_P,
	KEY_Z,
	KEY_S,
	KEY_A,
	KEY_W,
	KEY_2,
	KEY_LEFTMETA,
	KEY_D,		/* 0x20 */
	KEY_C,
	KEY_X,
	KEY_D,
	KEY_E,
	KEY_4,
	KEY_3,
	KEY_LEFTALT,
	KEY_APOSTROPHE,
	KEY_SPACE,
	KEY_V,
	KEY_F,
	KEY_T,
	KEY_R,
	KEY_5,
	KEY_MENU,
	KEY_B,		/* 0x30 */
	KEY_N,
	KEY_B,
	KEY_H,
	KEY_G,
	KEY_Y,
	KEY_6,
	KEY_KPASTERISK,
	KEY_LEFTALT,
	KEY_SPACE,
	KEY_M,
	KEY_J,
	KEY_U,
	KEY_7,
	KEY_8,
	KEY_F5,
	KEY_F6,		/* 0x40 */
	KEY_COMMA,
	KEY_K,
	KEY_I,
	KEY_O,
	KEY_0,
	KEY_9,		/* VK_SCROLL */
	KEY_KP7,
	KEY_KP8,
	KEY_DOT,
	KEY_SLASH,
	KEY_L,
	KEY_SEMICOLON,
	KEY_P,
	KEY_MINUS,
	KEY_KP1,
	KEY_KP2,	/* 0x50 */
	KEY_KP3,
	KEY_APOSTROPHE,
	KEY_KPDOT,
	KEY_LEFTBRACE,	/* VK_SNAPSHOT */
	KEY_EQUAL,
	KEY_102ND,	/* VK_OEM_102 */
	KEY_F11,	/* VK_F11 */
	KEY_CAPSLOCK,	/* VK_F12 */
	KEY_RIGHTSHIFT,
	KEY_ENTER,
	KEY_RIGHTBRACE,
	0,
	KEY_BACKSLASH,
	0,
	0,
	0,		/* 0x60 */
	0,
	0,
	KEY_SEARCH,	/* add search key map */
	0,
	0,
	KEY_BACKSPACE,
	0,
	0,
	0,
	0,
	KEY_LEFT,
	0,
	0,
	0,
	0,
	0,		/* 0x70 */
	0,
	KEY_DOWN,
	KEY_KP5,
	KEY_RIGHT,
	KEY_UP,
	KEY_ESC,
	0,
	0,
	0,
	KEY_PAGEDOWN,
	0,
	0,
	KEY_PAGEUP,
};

static unsigned short extcode_tf101[] = {
	0,
	KEY_DELETE,
	KEY_F1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	KEY_F6,
	KEY_F7,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	KEY_F8,
	KEY_F9,
	KEY_F10,
	KEY_F11,
	KEY_F12,
	KEY_SYSRQ,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,		/* 0xE0 0x10 */
};
