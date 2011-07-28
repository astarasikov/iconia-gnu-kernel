/*
 * arch/arm/mach-tegra/cpu-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>

#include <asm/smp_twd.h>
#include <asm/system.h>

#include <mach/hardware.h>
#include <mach/clk.h>

#include "clock.h"
#include "fuse.h"

/*
 * Frequency table index must be sequential starting at 0 and frequencies
 * must be ascending.
 */

struct tegra_cpufreq_table_data {
	struct cpufreq_frequency_table *freq_table;
	int throttle_lowest_index;
	int throttle_highest_index;
};

static struct cpufreq_frequency_table freq_table_750MHz[] = {
	{ 0, 216000 },
	{ 1, 312000 },
	{ 2, 456000 },
	{ 3, 608000 },
	{ 4, 750000 },
	{ 5, CPUFREQ_TABLE_END },
};

static struct cpufreq_frequency_table freq_table_1000GHz[] = {
	{ 0, 216000 },
	{ 1, 312000 },
	{ 2, 456000 },
	{ 3, 608000 },
	{ 4, 760000 },
	{ 5, 816000 },
	{ 6, 912000 },
	{ 7, 1000000 },
	{ 8, CPUFREQ_TABLE_END },
};

static struct cpufreq_frequency_table freq_table_1200GHz[] = {
	{ 0, 216000 },
	{ 1, 312000 },
	{ 2, 456000 },
	{ 3, 608000 },
	{ 4, 760000 },
	{ 5, 816000 },
	{ 6, 912000 },
	{ 7, 1000000 },
	{ 8, 1200000 },
	{ 9, CPUFREQ_TABLE_END },
};

static struct tegra_cpufreq_table_data cpufreq_tables[] = {
	{ freq_table_750MHz,  1, 4 },
	{ freq_table_1000GHz, 2, 6 },
	{ freq_table_1200GHz, 2, 7 },
};

static struct cpufreq_frequency_table *freq_table;

#define NUM_CPUS	2

static struct clk *cpu_clk;
static struct clk *emc_clk;

static unsigned long target_cpu_speed[NUM_CPUS];
static DEFINE_MUTEX(tegra_cpu_lock);
static bool is_suspended;

unsigned int tegra_getspeed(unsigned int cpu);
static int tegra_update_cpu_speed(unsigned long rate);

static unsigned long tegra_cpu_highest_speed(void);

#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
/* CPU frequency is gradually lowered when throttling is enabled */
#define THROTTLE_DELAY		msecs_to_jiffies(2000)

static bool is_throttling;
static int throttle_lowest_index;
static int throttle_highest_index;
static int throttle_index;
static int throttle_next_index;
static struct delayed_work throttle_work;
static struct workqueue_struct *workqueue;

#define tegra_cpu_is_throttling() (is_throttling)

static void tegra_throttle_work_func(struct work_struct *work)
{
	unsigned int current_freq;

	mutex_lock(&tegra_cpu_lock);

	current_freq = tegra_getspeed(0);
	throttle_index = throttle_next_index;

	if (freq_table[throttle_index].frequency < current_freq)
		tegra_update_cpu_speed(freq_table[throttle_index].frequency);

	if (throttle_index > throttle_lowest_index) {
		throttle_next_index = throttle_index - 1;
		queue_delayed_work(workqueue, &throttle_work, THROTTLE_DELAY);
	}

	mutex_unlock(&tegra_cpu_lock);
}

/*
 * tegra_throttling_enable
 * This function may sleep
 */
void tegra_throttling_enable(bool enable)
{
	mutex_lock(&tegra_cpu_lock);

	if (enable && !is_throttling) {
		unsigned int current_freq = tegra_getspeed(0);

		is_throttling = true;
		for (throttle_index = throttle_highest_index;
		     throttle_index >= throttle_lowest_index;
		     throttle_index--)
			if (freq_table[throttle_index].frequency
			    < current_freq)
				break;

		throttle_index = max(throttle_index, throttle_lowest_index);
		throttle_next_index = throttle_index;
		queue_delayed_work(workqueue, &throttle_work, 0);

	} else if (!enable && is_throttling) {
		cancel_delayed_work_sync(&throttle_work);
		is_throttling = false;
		/* restore speed requested by governor */
		tegra_update_cpu_speed(tegra_cpu_highest_speed());
	}

	mutex_unlock(&tegra_cpu_lock);
}
EXPORT_SYMBOL_GPL(tegra_throttling_enable);

static unsigned int throttle_governor_speed(unsigned int requested_speed)
{
	return tegra_cpu_is_throttling() ?
		min(requested_speed, freq_table[throttle_index].frequency) :
		requested_speed;
}

static ssize_t show_throttle(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%u\n", is_throttling);
}

cpufreq_freq_attr_ro(throttle);

#ifdef CONFIG_DEBUG_FS
static int throttle_debug_set(void *data, u64 val)
{
	tegra_throttling_enable(val);
	return 0;
}

static int throttle_debug_get(void *data, u64 *val)
{
	*val = (u64) is_throttling;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(throttle_fops, throttle_debug_get, throttle_debug_set, "%llu\n");

static struct dentry *cpu_tegra_debugfs_root;

static int __init tegra_cpu_debug_init(void)
{
	cpu_tegra_debugfs_root = debugfs_create_dir("cpu-tegra", 0);

	if (!cpu_tegra_debugfs_root)
		return -ENOMEM;

	if (!debugfs_create_file("throttle", 0644, cpu_tegra_debugfs_root, NULL, &throttle_fops))
		goto err_out;

	return 0;

err_out:
	debugfs_remove_recursive(cpu_tegra_debugfs_root);
	return -ENOMEM;

}

static void __exit tegra_cpu_debug_exit(void)
{
	debugfs_remove_recursive(cpu_tegra_debugfs_root);
}

late_initcall(tegra_cpu_debug_init);
module_exit(tegra_cpu_debug_exit);
#endif /* CONFIG_DEBUG_FS */

#else /* CONFIG_TEGRA_THERMAL_THROTTLE */
#define tegra_cpu_is_throttling() (0)
#define throttle_governor_speed(requested_speed) (requested_speed)

void tegra_throttling_enable(bool enable)
{
}
#endif /* CONFIG_TEGRA_THERMAL_THROTTLE */

int tegra_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

unsigned int tegra_getspeed(unsigned int cpu)
{
	unsigned long rate;

	if (cpu >= NUM_CPUS)
		return 0;

	rate = clk_get_rate(cpu_clk) / 1000;
	return rate;
}

static int tegra_update_cpu_speed(unsigned long rate)
{
	int ret = 0;
	struct cpufreq_freqs freqs;

	freqs.old = tegra_getspeed(0);
	freqs.new = rate;

	if (freqs.old == freqs.new)
		return ret;

	/*
	 * Vote on memory bus frequency based on cpu frequency
	 * This sets the minimum frequency, display or avp may request higher
	 */
	if (rate >= 816000)
		clk_set_rate(emc_clk, 600000000); /* cpu 816 MHz, emc max */
	else if (rate >= 456000)
		clk_set_rate(emc_clk, 300000000); /* cpu 456 MHz, emc 150Mhz */
	else
		clk_set_rate(emc_clk, 100000000);  /* emc 50Mhz */

	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_DEBUG "cpufreq-tegra: transition: %u --> %u\n",
	       freqs.old, freqs.new);
#endif

	ret = clk_set_rate(cpu_clk, freqs.new * 1000);
	if (ret) {
		pr_err("cpu-tegra: Failed to set cpu frequency to %d kHz\n",
			freqs.new);
		return ret;
	}

	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static unsigned long tegra_cpu_highest_speed(void)
{
	unsigned long rate = 0;
	int i;

	for_each_online_cpu(i)
		rate = max(rate, target_cpu_speed[i]);
	return rate;
}

static int tegra_target(struct cpufreq_policy *policy,
		       unsigned int target_freq,
		       unsigned int relation)
{
	int idx;
	unsigned int freq;
	unsigned int new_speed;
	int ret = 0;

	mutex_lock(&tegra_cpu_lock);

	if (is_suspended) {
		ret = -EBUSY;
		goto out;
	}

	cpufreq_frequency_table_target(policy, freq_table, target_freq,
		relation, &idx);

	freq = freq_table[idx].frequency;

	target_cpu_speed[policy->cpu] = freq;
	new_speed = throttle_governor_speed(tegra_cpu_highest_speed());
	ret = tegra_update_cpu_speed(new_speed);
out:
	mutex_unlock(&tegra_cpu_lock);
	return ret;
}


static int tegra_pm_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	mutex_lock(&tegra_cpu_lock);
	if (event == PM_SUSPEND_PREPARE) {
		is_suspended = true;
		pr_info("Tegra cpufreq suspend: setting frequency to %d kHz\n",
			freq_table[0].frequency);
		tegra_update_cpu_speed(freq_table[0].frequency);
	} else if (event == PM_POST_SUSPEND) {
		is_suspended = false;
	}
	mutex_unlock(&tegra_cpu_lock);

	return NOTIFY_OK;
}

static struct notifier_block tegra_cpu_pm_notifier = {
	.notifier_call = tegra_pm_notify,
};

static int tegra_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu >= NUM_CPUS)
		return -EINVAL;

	cpu_clk = clk_get_sys(NULL, "cpu");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	emc_clk = clk_get_sys("cpu", "emc");
	if (IS_ERR(emc_clk)) {
		clk_put(cpu_clk);
		return PTR_ERR(emc_clk);
	}

	clk_enable(emc_clk);
	clk_enable(cpu_clk);

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = tegra_getspeed(policy->cpu);
	target_cpu_speed[policy->cpu] = policy->cur;

	/* cpu clock change latency: ~400us */
	policy->cpuinfo.transition_latency = 400;

	policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;
	cpumask_copy(policy->related_cpus, cpu_possible_mask);

	if (policy->cpu == 0) {
		register_pm_notifier(&tegra_cpu_pm_notifier);
	}

	return 0;
}

static int tegra_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_disable(emc_clk);
	clk_put(emc_clk);
	clk_put(cpu_clk);
	return 0;
}

static struct freq_attr *tegra_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	&throttle,
#endif
	NULL,
};

static struct cpufreq_driver tegra_cpufreq_driver = {
	.verify		= tegra_verify_speed,
	.target		= tegra_target,
	.get		= tegra_getspeed,
	.init		= tegra_cpu_init,
	.exit		= tegra_cpu_exit,
	.name		= "tegra",
	.attr		= tegra_cpufreq_attr,
};

static struct tegra_cpufreq_table_data *tegra_cpufreq_table_get(void)
{
	int i;
	struct tegra_cpufreq_table_data *ret;
	struct clk *cpu_clk = tegra_get_clock_by_name("cpu");

	for (i = 0; i < ARRAY_SIZE(cpufreq_tables); i++) {
		struct cpufreq_policy policy;
		cpufreq_frequency_table_cpuinfo(&policy,
			cpufreq_tables[i].freq_table);
		if ((policy.max * 1000) == cpu_clk->max_rate) {
			ret = &cpufreq_tables[i];
			goto out;
		}
	}
	pr_err("%s: No cpufreq table matching cpu range", __func__);
	ret = &cpufreq_tables[0];
out:
	return ret;
}

static int __init tegra_cpufreq_init(void)
{
	struct tegra_cpufreq_table_data *table_data;
	table_data = tegra_cpufreq_table_get();

#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	/*
	 * High-priority, others flags default: not bound to a specific
	 * CPU, has rescue worker task (in case of allocation deadlock,
	 * etc.).  Single-threaded.
	 */
	workqueue = alloc_workqueue("cpu-tegra",
				    WQ_HIGHPRI | WQ_UNBOUND | WQ_RESCUER, 1);
	if (!workqueue)
		return -ENOMEM;
	INIT_DELAYED_WORK(&throttle_work, tegra_throttle_work_func);

	throttle_lowest_index = table_data->throttle_lowest_index;
	throttle_highest_index = table_data->throttle_highest_index;
#endif
	freq_table = table_data->freq_table;
	return cpufreq_register_driver(&tegra_cpufreq_driver);
}

static void __exit tegra_cpufreq_exit(void)
{
#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	destroy_workqueue(workqueue);
#endif
        cpufreq_unregister_driver(&tegra_cpufreq_driver);
}


MODULE_AUTHOR("Colin Cross <ccross@android.com>");
MODULE_DESCRIPTION("cpufreq driver for Nvidia Tegra2");
MODULE_LICENSE("GPL");
module_init(tegra_cpufreq_init);
module_exit(tegra_cpufreq_exit);
