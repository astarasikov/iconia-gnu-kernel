#ifndef __OF_IRQ_H
#define __OF_IRQ_H

#if defined(CONFIG_OF)
struct of_irq;
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/of.h>

/*
 * irq_of_parse_and_map() is used ba all OF enabled platforms; but SPARC
 * implements it differently.  However, the prototype is the same for all,
 * so declare it here regardless of the CONFIG_OF_IRQ setting.
 */
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);

#if defined(CONFIG_OF_IRQ)
/**
 * of_irq - container for device_node/irq_specifier pair for an irq controller
 * @controller: pointer to interrupt controller device tree node
 * @size: size of interrupt specifier
 * @specifier: array of cells @size long specifing the specific interrupt
 *
 * This structure is returned when an interrupt is mapped. The controller
 * field needs to be put() after use
 */
#define OF_MAX_IRQ_SPEC		4 /* We handle specifiers of at most 4 cells */
struct of_irq {
	struct device_node *controller; /* Interrupt controller node */
	u32 size; /* Specifier size */
	u32 specifier[OF_MAX_IRQ_SPEC]; /* Specifier copy */
};

/**
 * struct of_irq_domain - Translation domain from device tree to linux irq
 * @list: Linked list node entry
 * @match: (optional) Called to determine if the passed device_node
 *         interrupt-controller can be translated by this irq domain.
 *         Returns 'true' if it can.
 * @decode: Translation callback; returns virq, or NO_IRQ if this irq
 *          domain cannot translate it.
 * @controller: (optional) pointer to OF node.  By default, if
 *              'match' is not set, then this of_irq_domain will only
 *              be used if the device tree node passed in matches the
 *              controller pointer.
 * @priv: Private data pointer, not touched by core of_irq_domain code.
 */
struct of_irq_domain {
	struct list_head list;
	bool (*match)(struct of_irq_domain *d, struct device_node *np);
	unsigned int (*map)(struct of_irq_domain *d, struct device_node *np,
			    const u32 *intspec, u32 intsize);
	struct device_node *controller;
	void *priv;
};

/**
 * of_irq_domain_add() - Add a device tree interrupt translation domain
 * @domain: interrupt domain to add.
 */
extern void of_irq_domain_add(struct of_irq_domain *domain);
extern void of_irq_set_default_domain(struct of_irq_domain *host);
extern struct of_irq_domain *of_irq_domain_find(struct device_node *controller);
extern void of_irq_domain_add_simple(struct device_node *controller,
				     int irq_start, int irq_size);

/*
 * Workarounds only applied to 32bit powermac machines
 */
#define OF_IMAP_OLDWORLD_MAC	0x00000001
#define OF_IMAP_NO_PHANDLE	0x00000002

#if defined(CONFIG_PPC32) && defined(CONFIG_PPC_PMAC)
extern unsigned int of_irq_workarounds;
extern struct device_node *of_irq_dflt_pic;
extern int of_irq_map_oldworld(struct device_node *device, int index,
			       struct of_irq *out_irq);
#else /* CONFIG_PPC32 && CONFIG_PPC_PMAC */
#define of_irq_workarounds (0)
#define of_irq_dflt_pic (NULL)
static inline int of_irq_map_oldworld(struct device_node *device, int index,
				      struct of_irq *out_irq)
{
	return -EINVAL;
}
#endif /* CONFIG_PPC32 && CONFIG_PPC_PMAC */


extern int of_irq_map_raw(struct device_node *parent, const u32 *intspec,
			  u32 ointsize, const u32 *addr,
			  struct of_irq *out_irq);
extern int of_irq_map_one(struct device_node *device, int index,
			  struct of_irq *out_irq);
extern unsigned int irq_create_of_mapping(struct device_node *controller,
					  const u32 *intspec,
					  unsigned int intsize);
extern int of_irq_to_resource(struct device_node *dev, int index,
			      struct resource *r);
extern int of_irq_count(struct device_node *dev);
extern int of_irq_to_resource_table(struct device_node *dev,
		struct resource *res, int nr_irqs);

#endif /* CONFIG_OF_IRQ */
#endif /* CONFIG_OF */
#endif /* __OF_IRQ_H */
