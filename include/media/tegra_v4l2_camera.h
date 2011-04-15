/*
 * Platform data for TEGRA camera host
 *
 * Copyright (C) 2010, NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _TEGRA_CAMERA_H_
#define _TEGRA_CAMERA_H_

#include <linux/regulator/consumer.h>
#include <linux/i2c.h>

#include <mach/nvhost.h>

struct tegra_camera_platform_data {
	int	(*enable_camera)(struct nvhost_device *ndev);
	void	(*disable_camera)(struct nvhost_device *ndev);
	bool	flip_h;
	bool	flip_v;
};

#endif /* _TEGRA_CAMERA_H_ */
