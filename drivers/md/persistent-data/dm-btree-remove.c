/*
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-btree.h"
#include "dm-btree-internal.h"
#include "dm-transaction-manager.h"

/*
 * Removing an entry from a btree
 * ==============================
 *
 * A very important constraint for our btree is that no node, except the
 * root, may have fewer than a certain number of entries.
 * (MIN_ENTRIES <= nr_entries <= MAX_ENTRIES).
 *
 * Ensuring this is complicated by the way we want to only ever hold the
 * locks on 2 nodes concurrently, and only change nodes in a top to bottom
 * fashion.
 *
 * Each node may have a left or right sibling.  When decending the spine,
 * if a node contains only MIN_ENTRIES then we try and increase this to at
 * least MIN_ENTRIES + 1.  We do this in the following ways:
 *
 * [A] No siblings => this can only happen if the node is the root, in which
 *     case we copy the childs contents over the root.
 *
 * [B] No left sibling
 *     ==> rebalance(node, right sibling)
 *
 * [C] No right sibling
 *     ==> rebalance(left sibling, node)
 *
 * [D] Both siblings, total_entries(left, node, right) <= DEL_THRESHOLD
 *     ==> delete node adding it's contents to left and right
 *
 * [E] Both siblings, total_entries(left, node, right) > DEL_THRESHOLD
 *     ==> rebalance(left, node, right)
 *
 * After these operations it's possible that the our original node no
 * longer contains the desired sub tree.  For this reason this rebalancing
 * is performed on the children of the current node.  This also avoids
 * having a special case for the root.
 *
 * Once this rebalancing has occurred we can then step into the child node
 * for internal nodes.  Or delete the entry for leaf nodes.
 */

/*
 * Some little utilities for moving node data around.
 */
static void node_shift(struct node *n, int shift)
{
	uint32_t nr_entries = le32_to_cpu(n->header.nr_entries);

	if (shift < 0) {
		shift = -shift;
		memmove(key_ptr(n, 0),
			key_ptr(n, shift),
			(nr_entries - shift) * sizeof(__le64));
		memmove(value_ptr(n, 0, sizeof(__le64)),
			value_ptr(n, shift, sizeof(__le64)),
			(nr_entries - shift) * sizeof(__le64));
	} else {
		memmove(key_ptr(n, shift),
			key_ptr(n, 0),
			nr_entries * sizeof(__le64));
		memmove(value_ptr(n, shift, sizeof(__le64)),
			value_ptr(n, 0, sizeof(__le64)),
			nr_entries * sizeof(__le64));
	}
}

static void node_copy(struct node *left, struct node *right, int shift)
{
	uint32_t nr_left = le32_to_cpu(left->header.nr_entries);

	if (shift < 0) {
		shift = -shift;
		memcpy(key_ptr(left, nr_left),
		       key_ptr(right, 0),
		       shift * sizeof(__le64));
		memcpy(value_ptr(left, nr_left, sizeof(__le64)),
		       value_ptr(right, 0, sizeof(__le64)),
		       shift * sizeof(__le64));
	} else {
		memcpy(key_ptr(right, 0),
		       key_ptr(left, nr_left - shift),
		       shift * sizeof(__le64));
		memcpy(value_ptr(right, 0, sizeof(__le64)),
		       value_ptr(left, nr_left - shift, sizeof(__le64)),
		       shift * sizeof(__le64));
	}
}

/*
 * Delete a specific entry from a leaf node.
 */
static void delete_at(struct node *n, unsigned index, size_t value_size)
{
	unsigned nr_entries = le32_to_cpu(n->header.nr_entries);
	unsigned nr_to_copy = nr_entries - (index + 1);

	if (nr_to_copy) {
		memmove(key_ptr(n, index),
			key_ptr(n, index + 1),
			nr_to_copy * sizeof(__le64));

		memmove(value_ptr(n, index, value_size),
			value_ptr(n, index + 1, value_size),
			nr_to_copy * value_size);
	}

	n->header.nr_entries = cpu_to_le32(nr_entries - 1);
}

static unsigned del_threshold(struct node *n)
{
	return le32_to_cpu(n->header.max_entries) / 3;
}

static unsigned merge_threshold(struct node *n)
{
	/*
	 * The extra one is because we know we're potentially going to
	 * delete an entry.
	 */
	return 2 * (le32_to_cpu(n->header.max_entries) / 3) + 1;
}

struct child {
	unsigned index;
	struct dm_block *block;
	struct node *n;
};

static struct dm_btree_value_type le64_type = {
	.context = NULL,
	.size = sizeof(__le64),
	.inc = NULL,
	.dec = NULL,
	.equal = NULL
};

static int init_child(struct dm_btree_info *info, struct node *parent,
		      unsigned index, struct child *result)
{
	int r, inc;
	dm_block_t root;

	result->index = index;
	root = value64(parent, index);

	r = dm_tm_shadow_block(info->tm, root, &btree_node_validator,
			       &result->block, &inc);
	if (r)
		return r;

	result->n = dm_block_data(result->block);

	if (inc)
		inc_children(info->tm, result->n, &le64_type);

	return 0;
}

static int exit_child(struct dm_btree_info *info, struct child *c)
{
	return dm_tm_unlock(info->tm, c->block);
}

static void shift(struct node *left, struct node *right, int count)
{
	if (!count)
		return;

	if (count > 0) {
		node_shift(right, count);
		node_copy(left, right, count);
	} else {
		node_copy(left, right, count);
		node_shift(right, count);
	}

	left->header.nr_entries =
		cpu_to_le32(le32_to_cpu(left->header.nr_entries) - count);

	right->header.nr_entries =
		cpu_to_le32(le32_to_cpu(right->header.nr_entries) + count);
}

static void __rebalance2(struct dm_btree_info *info, struct node *parent,
			 struct child *l, struct child *r)
{
	struct node *left = l->n;
	struct node *right = r->n;
	uint32_t nr_left = le32_to_cpu(left->header.nr_entries);
	uint32_t nr_right = le32_to_cpu(right->header.nr_entries);

	if (nr_left + nr_right <= merge_threshold(left)) {
		/*
		 * Merge
		 */
		node_copy(left, right, -nr_right);
		left->header.nr_entries = cpu_to_le32(nr_left + nr_right);

		*((__le64 *) value_ptr(parent, l->index, sizeof(__le64))) =
			cpu_to_le64(dm_block_location(l->block));
		delete_at(parent, r->index, sizeof(__le64));

		/*
		 * We need to decrement the right block, but not it's
		 * children, since they're still referenced by left.
		 */
		dm_tm_dec(info->tm, dm_block_location(r->block));
	} else {
		/*
		 * Rebalance.
		 */
		unsigned target_left = (nr_left + nr_right) / 2;

		shift(left, right, nr_left - target_left);
		*((__le64 *) value_ptr(parent, l->index, sizeof(__le64))) =
			cpu_to_le64(dm_block_location(l->block));
		*((__le64 *) value_ptr(parent, r->index, sizeof(__le64))) =
			cpu_to_le64(dm_block_location(r->block));
		*key_ptr(parent, r->index) = right->keys[0];
	}
}

static int rebalance2(struct shadow_spine *s, struct dm_btree_info *info,
		      unsigned left_index)
{
	int r;
	struct node *parent;
	struct child left, right;

	parent = dm_block_data(shadow_current(s));

	r = init_child(info, parent, left_index, &left);
	if (r)
		return r;

	r = init_child(info, parent, left_index + 1, &right);
	if (r) {
		exit_child(info, &left);
		return r;
	}

	__rebalance2(info, parent, &left, &right);

	r = exit_child(info, &left);
	if (r) {
		exit_child(info, &right);
		return r;
	}

	r = exit_child(info, &right);
	if (r)
		return r;

	return 0;
}

static void __rebalance3(struct dm_btree_info *info, struct node *parent,
			 struct child *l, struct child *c, struct child *r)
{
	struct node *left = l->n;
	struct node *center = c->n;
	struct node *right = r->n;

	uint32_t nr_left = le32_to_cpu(left->header.nr_entries);
	uint32_t nr_center = le32_to_cpu(center->header.nr_entries);
	uint32_t nr_right = le32_to_cpu(right->header.nr_entries);
	uint32_t max_entries = le32_to_cpu(left->header.max_entries);

	unsigned target;

	if (((nr_left + nr_center + nr_right) / 2) < merge_threshold(center)) {
		/*
		 * Delete center node:
		 *
		 * We dump as many entries from center as possible into
		 * left, then the rest in right, then rebalance2.  This
		 * wastes some cpu, but I want something simple atm.
		 */
		unsigned shift = min(max_entries - nr_left, nr_center);

		node_copy(left, center, -shift);
		left->header.nr_entries = cpu_to_le32(nr_left + shift);

		if (shift != nr_center) {
			shift = nr_center - shift;
			node_shift(right, shift);
			node_copy(center, right, shift);
			right->header.nr_entries = cpu_to_le32(nr_right + shift);
		}

		*((__le64 *) value_ptr(parent, l->index, sizeof(__le64))) =
			cpu_to_le64(dm_block_location(l->block));
		*((__le64 *) value_ptr(parent, r->index, sizeof(__le64))) =
			cpu_to_le64(dm_block_location(r->block));
		*key_ptr(parent, r->index) = right->keys[0];

		delete_at(parent, c->index, sizeof(__le64));
		r->index--;

		dm_tm_dec(info->tm, dm_block_location(c->block));
		__rebalance2(info, parent, l, r);

		return;
	}

	/*
	 * Rebalance
	 */
	target = (nr_left + nr_center + nr_right) / 3;
	BUG_ON(target == nr_center);

	/*
	 * Adjust the left node
	 */
	shift(left, center, nr_left - target);

	/*
	 * Adjust the right node
	 */
	shift(center, right, target - nr_right);

	*((__le64 *) value_ptr(parent, l->index, sizeof(__le64))) =
		cpu_to_le64(dm_block_location(l->block));
	*((__le64 *) value_ptr(parent, c->index, sizeof(__le64))) =
		cpu_to_le64(dm_block_location(c->block));
	*((__le64 *) value_ptr(parent, r->index, sizeof(__le64))) =
		cpu_to_le64(dm_block_location(r->block));

	*key_ptr(parent, c->index) = center->keys[0];
	*key_ptr(parent, r->index) = right->keys[0];
}

static int rebalance3(struct shadow_spine *s, struct dm_btree_info *info,
		      unsigned left_index)
{
	int r;
	struct node *parent = dm_block_data(shadow_current(s));
	struct child left, center, right;

	/*
	 * FIXME: fill out an array?
	 */
	r = init_child(info, parent, left_index, &left);
	if (r)
		return r;

	r = init_child(info, parent, left_index + 1, &center);
	if (r) {
		exit_child(info, &left);
		return r;
	}

	r = init_child(info, parent, left_index + 2, &right);
	if (r) {
		exit_child(info, &left);
		exit_child(info, &center);
		return r;
	}

	__rebalance3(info, parent, &left, &center, &right);

	r = exit_child(info, &left);
	if (r) {
		exit_child(info, &center);
		exit_child(info, &right);
		return r;
	}

	r = exit_child(info, &center);
	if (r) {
		exit_child(info, &right);
		return r;
	}

	r = exit_child(info, &right);
	if (r)
		return r;

	return 0;
}

static int get_nr_entries(struct dm_transaction_manager *tm,
			  dm_block_t b, uint32_t *result)
{
	int r;
	struct dm_block *block;
	struct node *n;

	r = dm_tm_read_lock(tm, b, &btree_node_validator, &block);
	if (r)
		return r;

	n = dm_block_data(block);
	*result = le32_to_cpu(n->header.nr_entries);

	return dm_tm_unlock(tm, block);
}

static int rebalance_children(struct shadow_spine *s,
			      struct dm_btree_info *info, uint64_t key)
{
	int i, r, has_left_sibling, has_right_sibling;
	uint32_t child_entries;
	struct node *n;

	n = dm_block_data(shadow_current(s));

	if (le32_to_cpu(n->header.nr_entries) == 1) {
		struct dm_block *child;
		dm_block_t b = value64(n, 0);

		r = dm_tm_read_lock(info->tm, b, &btree_node_validator, &child);
		if (r)
			return r;

		memcpy(n, dm_block_data(child),
		       dm_bm_block_size(dm_tm_get_bm(info->tm)));
		r = dm_tm_unlock(info->tm, child);
		dm_tm_dec(info->tm, dm_block_location(child));

		return r;
	}

	i = lower_bound(n, key);
	if (i < 0)
		return -ENODATA;

	r = get_nr_entries(info->tm, value64(n, i), &child_entries);
	if (r)
		return r;

	if (child_entries > del_threshold(n))
		return 0;

	has_left_sibling = i > 0 ? 1 : 0;
	has_right_sibling =
		(i >= (le32_to_cpu(n->header.nr_entries) - 1)) ? 0 : 1;

	if (!has_left_sibling)
		r = rebalance2(s, info, i);

	else if (!has_right_sibling)
		r = rebalance2(s, info, i - 1);

	else
		r = rebalance3(s, info, i - 1);

	return r;
}

static int do_leaf(struct node *n, uint64_t key, unsigned *index)
{
	int i = lower_bound(n, key);

	if ((i < 0) ||
	    (i >= le32_to_cpu(n->header.nr_entries)) ||
	    (le64_to_cpu(n->keys[i]) != key))
		return -ENODATA;

	*index = i;

	return 0;
}

/*
 * Prepares for removal from one level of the hierarchy.  The caller must
 * actually call delete_at() to remove the entry at index.
 */
static int remove_raw(struct shadow_spine *s, struct dm_btree_info *info,
		      struct dm_btree_value_type *vt, dm_block_t root,
		      uint64_t key, unsigned *index)
{
	int i = *index, inc, r;
	struct node *n;

	for (;;) {
		r = shadow_step(s, root, vt, &inc);
		if (r < 0)
			break;

		/*
		 * We have to patch up the parent node, ugly, but I don't
		 * see a way to do this automatically as part of the spine
		 * op.
		 */
		if (shadow_has_parent(s)) {
			__le64 location = cpu_to_le64(dm_block_location(shadow_current(s)));
			memcpy(value_ptr(dm_block_data(shadow_parent(s)), i, sizeof(uint64_t)),
			       &location, sizeof(__le64));
		}

		n = dm_block_data(shadow_current(s));
		if (inc)
			inc_children(info->tm, n, vt);

		if (le32_to_cpu(n->header.flags) & LEAF_NODE)
			return do_leaf(n, key, index);

		r = rebalance_children(s, info, key);
		if (r)
			break;

		n = dm_block_data(shadow_current(s));
		if (le32_to_cpu(n->header.flags) & LEAF_NODE)
			return do_leaf(n, key, index);

		i = lower_bound(n, key);

		/*
		 * We know the key is present, or else
		 * rebalance_children would have returned
		 * -ENODATA
		 */
		root = value64(n, i);
	}

	return r;
}

int dm_btree_remove(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, dm_block_t *new_root)
{
	unsigned level, last_level = info->levels - 1;
	int index = 0, r = 0;
	struct shadow_spine spine;
	struct node *n;

	init_shadow_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		r = remove_raw(&spine, info,
			       (level == last_level ?
				&info->value_type : &le64_type),
			       root, keys[level], (unsigned *)&index);
		if (r < 0)
			break;

		n = dm_block_data(shadow_current(&spine));
		if (level != last_level) {
			root = value64(n, index);
			continue;
		}

		BUG_ON(index < 0 || index >= le32_to_cpu(n->header.nr_entries));

		if (info->value_type.dec)
			info->value_type.dec(info->value_type.context,
					     value_ptr(n, index, info->value_type.size));

		delete_at(n, index, info->value_type.size);

		r = 0;
		*new_root = shadow_root(&spine);
	}

	exit_shadow_spine(&spine);

	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_remove);
