/*
 * Clock infrastructure for device tree platforms
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>

struct of_clk_provider {
	struct list_head	link;
	struct device_node	*node;

	/* Return NULL if no such clock output */
	struct clk		*(*get)(struct device_node *np,
					const char *output_id, void *data);
	void			*data;
};

static LIST_HEAD(of_clk_providers);
static DEFINE_MUTEX(of_clk_lock);

int of_clk_add_provider(struct device_node *np,
		struct clk *(*clk_src_get)(struct device_node *np,
			const char *output_id,
			void *data),
		void *data)
{
	struct of_clk_provider *cp;

	cp = kzalloc(sizeof(struct of_clk_provider), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	cp->node = of_node_get(np);
	cp->data = data;
	cp->get = clk_src_get;

	mutex_lock(&of_clk_lock);
	list_add(&cp->link, &of_clk_providers);
	mutex_unlock(&of_clk_lock);
	pr_debug("Added clock from %s\n", np->full_name);

	return 0;
}

void of_clk_del_provider(struct device_node *np,
		struct clk *(*clk_src_get)(struct device_node *np,
			const char *output_id,
			void *data),
		void *data)
{
	struct of_clk_provider *cp, *tmp;

	mutex_lock(&of_clk_lock);
	list_for_each_entry_safe(cp, tmp, &of_clk_providers, link) {
		if (cp->node == np && cp->get == clk_src_get &&
				cp->data == data) {
			list_del(&cp->link);
			of_node_put(cp->node);
			kfree(cp);
			break;
		}
	}
	mutex_unlock(&of_clk_lock);
}

static struct clk *__of_clk_get_from_provider(struct device_node *np, const char *clk_output)
{
	struct of_clk_provider *provider;
	struct clk *clk = NULL;

	/* Check if we have such a provider in our array */
	mutex_lock(&of_clk_lock);
	list_for_each_entry(provider, &of_clk_providers, link) {
		if (provider->node == np)
			clk = provider->get(np, clk_output, provider->data);
		if (clk)
			break;
	}
	mutex_unlock(&of_clk_lock);

	return clk;
}

struct clk *of_clk_get(struct device *dev, const char *id)
{
	struct device_node *provnode;
	u32 provhandle;
	int sz;
	struct clk *clk;
	char prop_name[32]; /* 32 is max size of property name */
	const void *prop;

	if (!dev)
		return NULL;
	dev_dbg(dev, "Looking up %s-clock from device tree\n", id);

	snprintf(prop_name, 32, "%s-clock", id ? id : "bus");
	prop = of_get_property(dev->of_node, prop_name, &sz);
	if (!prop || sz < 4)
		return NULL;

	/* Extract the phandle from the start of the property value */
	provhandle = be32_to_cpup(prop);
	prop += 4;
	sz -= 4;

	/* Make sure the clock name is properly terminated and within the
	 * size of the property. */
	if (strlen(prop) + 1 > sz)
		return NULL;

	/* Find the clock provider node; check if it is registered as a
	 * provider, and ask it for the relevant clk structure */
	provnode = of_find_node_by_phandle(provhandle);
	if (!provnode) {
		pr_warn("%s: %s property in node %s references invalid phandle",
			__func__, prop_name, dev->of_node->full_name);
		return NULL;
	}
	clk = __of_clk_get_from_provider(provnode, prop);
	if (clk)
		dev_dbg(dev, "Using clock from %s\n", provnode->full_name);

	of_node_put(provnode);

	return clk;
}

