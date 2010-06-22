/*
 *  i915_backlight.c - ChromeOS specific backlight support for pineview
 *
 *
 *  Copyright (C) 2010 ChromeOS contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/backlight.h>

#include "drmP.h"
#include "i915_drm.h"
#include "i915_drv.h"

/*
 * Somewhat arbitrarily choose a max brightness level of 256 (as full "on")
 * and a PWM frequency of 0x1000.  The frequency can be as high as 0x7fff,
 * but we do not need that level of flexibility.
 */
#define MAX_BRIGHTNESS 256
#define PWM_FREQUENCY 0x1000

/*
 * The Pineview LVDS Backlight PWM Control register is a 32 bit word split
 * into two unsigned 16 bit words: the high order short is the cycle frequency,
 * and the low order word is the duty cycle.  According to i915_opregion.c,
 * the low order bit of each short is unused.
 *
 * While the frequency is hardcoded, these macros provide masking and shifting
 * for the duty cycle.
 */
#define CTL_TO_PWM(ctl) ((ctl & BACKLIGHT_DUTY_CYCLE_MASK) >> 1)
#define PWM_TO_CTL(pwm) ((pwm << 1) & BACKLIGHT_DUTY_CYCLE_MASK)

static int i915_get_intensity(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 blc_pwm_ctl;
	int level, pwm_val;

	blc_pwm_ctl = I915_READ(BLC_PWM_CTL);
	pwm_val = CTL_TO_PWM(blc_pwm_ctl);
	level = (pwm_val * MAX_BRIGHTNESS) / PWM_FREQUENCY;

	return level;
}

static int i915_set_intensity(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	struct drm_i915_private *dev_priv = dev->dev_private;
	int level, pwm_val;
	u32 blc_pwm_ctl;

	level = bd->props.brightness;
	if (level > MAX_BRIGHTNESS)
		level = MAX_BRIGHTNESS;

	pwm_val = (level * PWM_FREQUENCY) / MAX_BRIGHTNESS;
	blc_pwm_ctl = (PWM_FREQUENCY << BACKLIGHT_MODULATION_FREQ_SHIFT) |
		PWM_TO_CTL(pwm_val);
	I915_WRITE(BLC_PWM_CTL, blc_pwm_ctl);

	return 0;
}

static struct backlight_ops i915_bl_ops = {
	.get_brightness = i915_get_intensity,
	.update_status = i915_set_intensity,
};

void i915_backlight_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct backlight_device *bd;

	if (!IS_PINEVIEW(dev)) {
		dev_printk(KERN_WARNING, &dev->pdev->dev,
		"i915_backlight_init only supports the pineview version\n");
		return;
	}

	bd = backlight_device_register("i915_backlight",
		&dev->pdev->dev, dev, &i915_bl_ops);
	if (IS_ERR(bd)) {
		dev_printk(KERN_WARNING, &dev->pdev->dev,
			"Unable to register i915 backlight.\n");
		return;
	}

	dev_priv->backlight = bd;
	bd->props.max_brightness = MAX_BRIGHTNESS;
	bd->props.brightness = 0;
	backlight_update_status(bd);
	return;
}

void i915_backlight_exit(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->backlight) {
		backlight_device_unregister(dev_priv->backlight);
		dev_priv->backlight = NULL;
	}
}
