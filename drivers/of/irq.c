/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used to make IRQ descriptions in the
 * device tree to actual irq numbers on an interrupt controller
 * driver.
 */

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/slab.h>

/* For archs that don't support NO_IRQ (such as x86), provide a dummy value */
#ifndef NO_IRQ
#define NO_IRQ 0
#endif

/*
 * Device Tree IRQ domains
 *
 * IRQ domains provide translation from device tree irq controller nodes to
 * linux IRQ numbers.  IRQ controllers register an irq_domain with a .map()
 * hook that performs everything needed to decode and configure a device
 * tree specified interrupt.
 */
static LIST_HEAD(of_irq_domains);
static DEFINE_RAW_SPINLOCK(of_irq_lock);
static struct of_irq_domain *of_irq_default_domain;

/**
 * of_irq_domain_default_match() - Return true if the controller pointers match
 *
 * Default match behaviour for of_irq_domains.  If the device tree node pointer
 * matches the value stored in the domain structure, then return true.
 */
static bool of_irq_domain_default_match(struct of_irq_domain *domain,
					struct device_node *controller)
{
	return domain->controller == controller;
}

/**
 * of_irq_domain_add() - Register a device tree irq domain
 * @domain: pointer to domain structure to be registered.
 *
 * Adds an of_irq_domain to the global list of domains.
 */
void of_irq_domain_add(struct of_irq_domain *domain)
{
	unsigned long flags;

	if (!domain->match)
		domain->match = of_irq_domain_default_match;
	if (!domain->map) {
		WARN_ON(1);
		return;
	}

	raw_spin_lock_irqsave(&of_irq_lock, flags);
	list_add(&domain->list, &of_irq_domains);
	raw_spin_unlock_irqrestore(&of_irq_lock, flags);
}

/**
 * of_irq_domain_find() - Find the domain that handles a given device tree node
 *
 * Returns the pointer to an of_irq_domain capable of translating irq specifiers
 * for the given irq controller device tree node.  Returns NULL if a suitable
 * domain could not be found.
 */
struct of_irq_domain *of_irq_domain_find(struct device_node *controller)
{
	struct of_irq_domain *domain, *found = NULL;
	unsigned long flags;

	raw_spin_lock_irqsave(&of_irq_lock, flags);
	list_for_each_entry(domain, &of_irq_domains, list) {
		if (domain->match(domain, controller)) {
			found = domain;
			break;
		}
	}
	raw_spin_unlock_irqrestore(&of_irq_lock, flags);
	return found;
}

/**
 * of_irq_set_default_domain() - Set a "default" host
 * @domain: default domain pointer
 *
 * For convenience, it's possible to set a "default" host that will be used
 * whenever NULL is passed to irq_of_create_mapping(). It makes life easier
 * for platforms that want to manipulate a few hard coded interrupt numbers
 * that aren't properly represented in the device-tree.
 */
void of_irq_set_default_domain(struct of_irq_domain *domain)
{
	pr_debug("irq: Default host set to @0x%p\n", domain);
	of_irq_default_domain = domain;
}

/**
 * irq_create_of_mapping() - Map a linux irq # from a device tree specifier
 * @controller - interrupt-controller node in the device tree
 * @intspec - array of interrupt specifier data.  Points to an array of u32
 *            values.  Data is *cpu-native* endian u32 values.
 * @intsize - size of intspec array.
 *
 * Given an interrupt controller node pointer and an interrupt specifier, this
 * function looks up the linux irq number.
 */
unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct of_irq_domain *domain;

	domain = of_irq_domain_find(controller);
	if (!domain)
		domain = of_irq_default_domain;
	if (!domain) {
		pr_warn("error: no irq host found for %s !\n",
			controller->full_name);
#if defined(CONFIG_MIPS) || defined(CONFIG_MICROBLAZE)
		/* FIXME: make Microblaze and MIPS register irq domains */
		return intspec[0];
#else /* defined(CONFIG_MIPS) || defined(CONFIG_MICROBLAZE) */
		return NO_IRQ;
#endif /* defined(CONFIG_MIPS) || defined(CONFIG_MICROBLAZE) */
	}

	return domain->map(domain, controller, intspec, intsize);
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

/*
 * A simple irq domain implementation that 1:1 translates hwirqs to an offset
 * from the irq_start value
 */
struct of_irq_domain_simple {
	struct of_irq_domain domain;
	int irq_start;
	int irq_size;
};

static unsigned int of_irq_domain_simple_map(struct of_irq_domain *domain,
					     struct device_node *controller,
					     const u32 *intspec, u32 intsize)
{
	struct of_irq_domain_simple *ds;

	ds = container_of(domain, struct of_irq_domain_simple, domain);
	if (intspec[0] >= ds->irq_size)
		return NO_IRQ;
	return ds->irq_start + intspec[0];
}

/**
 * of_irq_domain_create_simple() - Set up a 'simple' translation range
 */
void of_irq_domain_add_simple(struct device_node *controller,
			      int irq_start, int irq_size)
{
	struct of_irq_domain_simple *sd;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		WARN_ON(1);
		return;
	}

	sd->irq_start = irq_start;
	sd->irq_size = irq_size;
	sd->domain.controller = of_node_get(controller);
	sd->domain.map = of_irq_domain_simple_map;
	of_irq_domain_add(&sd->domain);
}

/**
 * irq_of_parse_and_map - Parse and map an interrupt into linux virq space
 * @device: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_map_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
unsigned int irq_of_parse_and_map(struct device_node *dev, int index)
{
	struct of_irq oirq;

	if (of_irq_map_one(dev, index, &oirq))
		return NO_IRQ;

	return irq_create_of_mapping(oirq.controller, oirq.specifier,
				     oirq.size);
}
EXPORT_SYMBOL_GPL(irq_of_parse_and_map);

/**
 * of_irq_find_parent - Given a device node, find its interrupt parent node
 * @child: pointer to device node
 *
 * Returns a pointer to the interrupt parent node, or NULL if the interrupt
 * parent could not be determined.
 */
static struct device_node *of_irq_find_parent(struct device_node *child)
{
	struct device_node *p;
	const __be32 *parp;

	if (!of_node_get(child))
		return NULL;

	do {
		parp = of_get_property(child, "interrupt-parent", NULL);
		if (parp == NULL)
			p = of_get_parent(child);
		else {
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				p = of_node_get(of_irq_dflt_pic);
			else
				p = of_find_node_by_phandle(be32_to_cpup(parp));
		}
		of_node_put(child);
		child = p;
	} while (p && of_get_property(p, "#interrupt-cells", NULL) == NULL);

	return p;
}

/**
 * of_irq_map_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @intspec:	interrupt specifier ("interrupts" property of the device)
 * @ointsize:   size of the passed in interrupt specifier
 * @addr:	address specifier (start of "reg" property of the device)
 * @out_irq:	structure of_irq filled by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent.
 */
int of_irq_map_raw(struct device_node *parent, const __be32 *intspec,
		   u32 ointsize, const __be32 *addr, struct of_irq *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	const __be32 *tmp, *imap, *imask;
	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

	pr_debug("of_irq_map_raw: par=%s,intspec=[0x%08x 0x%08x...],ointsize=%d\n",
		 parent->full_name, be32_to_cpup(intspec),
		 be32_to_cpup(intspec + 1), ointsize);

	ipar = of_node_get(parent);

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */
	do {
		tmp = of_get_property(ipar, "#interrupt-cells", NULL);
		if (tmp != NULL) {
			intsize = be32_to_cpu(*tmp);
			break;
		}
		tnode = ipar;
		ipar = of_irq_find_parent(ipar);
		of_node_put(tnode);
	} while (ipar);
	if (ipar == NULL) {
		pr_debug(" -> no parent found !\n");
		goto fail;
	}

	pr_debug("of_irq_map_raw: ipar=%s, size=%d\n", ipar->full_name, intsize);

	if (ointsize != intsize)
		return -EINVAL;

	/* Look for this #address-cells. We have to implement the old linux
	 * trick of looking for the parent here as some device-trees rely on it
	 */
	old = of_node_get(ipar);
	do {
		tmp = of_get_property(old, "#address-cells", NULL);
		tnode = of_get_parent(old);
		of_node_put(old);
		old = tnode;
	} while (old && tmp == NULL);
	of_node_put(old);
	old = NULL;
	addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

	pr_debug(" -> addrsize=%d\n", addrsize);

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		/* Now check if cursor is an interrupt-controller and if it is
		 * then we are done
		 */
		if (of_get_property(ipar, "interrupt-controller", NULL) !=
				NULL) {
			pr_debug(" -> got it !\n");
			for (i = 0; i < intsize; i++)
				out_irq->specifier[i] =
						of_read_number(intspec +i, 1);
			out_irq->size = intsize;
			out_irq->controller = ipar;
			of_node_put(old);
			return 0;
		}

		/* Now look for an interrupt-map */
		imap = of_get_property(ipar, "interrupt-map", &imaplen);
		/* No interrupt map, check for an interrupt parent */
		if (imap == NULL) {
			pr_debug(" -> no map, getting parent\n");
			newpar = of_irq_find_parent(ipar);
			goto skiplevel;
		}
		imaplen /= sizeof(u32);

		/* Look for a mask */
		imask = of_get_property(ipar, "interrupt-map-mask", NULL);

		/* If we were passed no "reg" property and we attempt to parse
		 * an interrupt-map, then #address-cells must be 0.
		 * Fail if it's not.
		 */
		if (addr == NULL && addrsize != 0) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}

		/* Parse interrupt-map */
		match = 0;
		while (imaplen > (addrsize + intsize + 1) && !match) {
			/* Compare specifiers */
			match = 1;
			for (i = 0; i < addrsize && match; ++i) {
				u32 mask = imask ? imask[i] : 0xffffffffu;
				match = ((addr[i] ^ imap[i]) & mask) == 0;
			}
			for (; i < (addrsize + intsize) && match; ++i) {
				u32 mask = imask ? imask[i] : 0xffffffffu;
				match =
				   ((intspec[i-addrsize] ^ imap[i]) & mask) == 0;
			}
			imap += addrsize + intsize;
			imaplen -= addrsize + intsize;

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			/* Get the interrupt parent */
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				newpar = of_node_get(of_irq_dflt_pic);
			else
				newpar = of_find_node_by_phandle(be32_to_cpup(imap));
			imap++;
			--imaplen;

			/* Check if not found */
			if (newpar == NULL) {
				pr_debug(" -> imap parent not found !\n");
				goto fail;
			}

			/* Get #interrupt-cells and #address-cells of new
			 * parent
			 */
			tmp = of_get_property(newpar, "#interrupt-cells", NULL);
			if (tmp == NULL) {
				pr_debug(" -> parent lacks #interrupt-cells!\n");
				goto fail;
			}
			newintsize = be32_to_cpu(*tmp);
			tmp = of_get_property(newpar, "#address-cells", NULL);
			newaddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

			pr_debug(" -> newintsize=%d, newaddrsize=%d\n",
			    newintsize, newaddrsize);

			/* Check for malformed properties */
			if (imaplen < (newaddrsize + newintsize))
				goto fail;

			imap += newaddrsize + newintsize;
			imaplen -= newaddrsize + newintsize;

			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

		of_node_put(old);
		old = of_node_get(newpar);
		addrsize = newaddrsize;
		intsize = newintsize;
		intspec = imap - intsize;
		addr = intspec - addrsize;

	skiplevel:
		/* Iterate again with new parent */
		pr_debug(" -> new parent: %s\n", newpar ? newpar->full_name : "<>");
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
 fail:
	of_node_put(ipar);
	of_node_put(old);
	of_node_put(newpar);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_irq_map_raw);

/**
 * of_irq_map_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_irq filled by this function
 *
 * This function resolves an interrupt, walking the tree, for a given
 * device-tree node. It's the high level pendant to of_irq_map_raw().
 */
int of_irq_map_one(struct device_node *device, int index, struct of_irq *out_irq)
{
	struct device_node *p;
	const __be32 *intspec, *tmp, *addr;
	u32 intsize, intlen;
	int res = -EINVAL;

	pr_debug("of_irq_map_one: dev=%s, index=%d\n", device->full_name, index);

	/* OldWorld mac stuff is "special", handle out of line */
	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return of_irq_map_oldworld(device, index, out_irq);

	/* Get the interrupts property */
	intspec = of_get_property(device, "interrupts", &intlen);
	if (intspec == NULL)
		return -EINVAL;
	intlen /= sizeof(*intspec);

	pr_debug(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	tmp = of_get_property(p, "#interrupt-cells", NULL);
	if (tmp == NULL)
		goto out;
	intsize = be32_to_cpu(*tmp);

	pr_debug(" intsize=%d intlen=%d\n", intsize, intlen);

	/* Check index */
	if ((index + 1) * intsize > intlen)
		goto out;

	/* Get new specifier and map it */
	res = of_irq_map_raw(p, intspec + index * intsize, intsize,
			     addr, out_irq);
 out:
	of_node_put(p);
	return res;
}
EXPORT_SYMBOL_GPL(of_irq_map_one);

/**
 * of_irq_to_resource - Decode a node's IRQ and return it as a resource
 * @dev: pointer to device tree node
 * @index: zero-based index of the irq
 * @r: pointer to resource structure to return result into.
 */
int of_irq_to_resource(struct device_node *dev, int index, struct resource *r)
{
	int irq = irq_of_parse_and_map(dev, index);

	/* Only dereference the resource if both the
	 * resource and the irq are valid. */
	if (r && irq != NO_IRQ) {
		r->start = r->end = irq;
		r->flags = IORESOURCE_IRQ;
		r->name = dev->full_name;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource);

/**
 * of_irq_count - Count the number of IRQs a node uses
 * @dev: pointer to device tree node
 */
int of_irq_count(struct device_node *dev)
{
	int nr = 0;

	while (of_irq_to_resource(dev, nr, NULL) != NO_IRQ)
		nr++;

	return nr;
}

/**
 * of_irq_to_resource_table - Fill in resource table with node's IRQ info
 * @dev: pointer to device tree node
 * @res: array of resources to fill in
 * @nr_irqs: the number of IRQs (and upper bound for num of @res elements)
 *
 * Returns the size of the filled in table (up to @nr_irqs).
 */
int of_irq_to_resource_table(struct device_node *dev, struct resource *res,
		int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++, res++)
		if (of_irq_to_resource(dev, i, res) == NO_IRQ)
			break;

	return i;
}
