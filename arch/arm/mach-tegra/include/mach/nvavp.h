/*
 * Copyright (C) 2011 Nvidia Corp
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

#ifndef __MEDIA_VIDEO_TEGRA_NVAVP_H
#define __MEDIA_VIDEO_TEGRA_NVAVP_H

#include <linux/tegra_avp.h>
#include <linux/tegra_sema.h>
#include <linux/tegra_rpc.h>

struct avp_info;

int tegra_avp_open(void);
int tegra_avp_release(void);
int tegra_avp_load_lib(struct tegra_avp_lib *lib);
int tegra_avp_unload_lib(unsigned long handle);


struct trpc_sema;

struct trpc_sema *tegra_sema_open(void);
int tegra_sema_release(struct trpc_sema *sema);
int tegra_sema_wait(struct trpc_sema *info, long *timeout);
int tegra_sema_signal(struct trpc_sema *info);


struct rpc_info;

struct rpc_info *tegra_rpc_open(void);
int tegra_rpc_release(struct rpc_info *info);
int tegra_rpc_port_create(struct rpc_info *info, char *name,
			  struct trpc_sema *sema);
int tegra_rpc_get_name(struct rpc_info *info, char* name);
int tegra_rpc_port_connect(struct rpc_info *info, long timeout);
int tegra_rpc_port_listen(struct rpc_info *info, long timeout);
int tegra_rpc_write(struct rpc_info *info, u8* buf, size_t size);
int tegra_rpc_read(struct rpc_info *info, u8 *buf, size_t max);


#endif
