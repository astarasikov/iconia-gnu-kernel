/*
 * Clock infrastructure for device tree platforms
 */
#ifndef __OF_CLK_H
#define __OF_CLK_H

struct device;
struct clk;

#ifdef CONFIG_OF_CLOCK

struct device_node;

int of_clk_add_provider(struct device_node *np,
		struct clk *(*clk_src_get)(struct device_node *np,
			const char *output_id,
			void *data),
		void *data);

void of_clk_del_provider(struct device_node *np,
		struct clk *(*clk_src_get)(struct device_node *np,
			const char *output_id,
			void *data),
		void *data);

struct clk *of_clk_get(struct device *dev, const char *id);

#else
static inline struct clk *of_clk_get(struct device *dev, const char *id)
{
	return NULL;
}
#endif

#endif /* __OF_CLK_H */

