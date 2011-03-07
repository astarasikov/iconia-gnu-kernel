/*
 * Copyright (C) 2011 NVIDIA Corp.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

#include <linux/tegra_mediaserver.h>
#include <mach/nvavp.h>
#include <mach/nvmap.h>

struct tegra_mediasrv_block {
	struct list_head entry;
	struct tegra_mediaserver_block_info block;
};

struct tegra_mediasrv_iram {
	struct list_head entry;
	struct tegra_mediaserver_iram_info iram;
};

struct tegra_mediasrv_node {
	struct tegra_mediasrv_info *mediasrv;
	struct list_head blocks;
	int nr_iram_shared;
};

struct tegra_mediasrv_manager {
	struct tegra_avp_lib   lib;
	struct rpc_info  *rpc;
	struct trpc_sema *sema;
};

struct tegra_mediasrv_info {
	int minor;
	struct mutex lock;
	struct nvmap_client *nvmap;
	struct tegra_mediasrv_manager manager;
	int nr_nodes;
	int nr_blocks;
	struct tegra_mediaserver_iram_info iram; /* only one supported */
	int nr_iram_shared;
};

static struct tegra_mediasrv_info *mediasrv_info;


/*
 * File entry points
 */
static int mediasrv_open(struct inode *inode, struct file *file)
{
	struct tegra_mediasrv_info *mediasrv = mediasrv_info;
	struct tegra_mediasrv_manager *manager = &mediasrv->manager;
	struct tegra_avp_lib *lib = &manager->lib;
	struct tegra_mediasrv_node *node = NULL;
	int ret = 0;

	node = kzalloc(sizeof(struct tegra_mediasrv_node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	INIT_LIST_HEAD(&node->blocks);
	node->mediasrv = mediasrv;

	mutex_lock(&mediasrv->lock);
	nonseekable_open(inode, file);
	file->private_data = node;

	if (mediasrv->nr_nodes++)
		goto out;

	manager->sema = tegra_sema_open();
	if (!manager->sema) {
		ret = -ENOMEM;
		goto fail_node_free;
	}

	manager->rpc = tegra_rpc_open();
	if (!manager->rpc) {
		ret = -ENOMEM;
		goto fail_sema_release;
	}

	ret = tegra_rpc_port_create(manager->rpc, "NVMM_MANAGER_SRV",
				    manager->sema);
	if (ret < 0)
		goto fail_rpc_release;

	ret = tegra_avp_open();
	if (ret < 0)
		goto fail_rpc_release;

	strcpy(lib->name, "nvmm_manager.axf");
	lib->args = &mediasrv;
	lib->args_len = sizeof(mediasrv);
	ret = tegra_avp_load_lib(lib);
	if (ret < 0)
		goto fail_avp_release;

	ret = tegra_rpc_port_connect(manager->rpc, 50000);
	if (ret < 0)
		goto fail_unload;

	goto out;

fail_unload:
	tegra_avp_unload_lib(lib->handle);
	lib->handle = 0;
fail_avp_release:
	tegra_avp_release();
fail_rpc_release:
	tegra_rpc_release(manager->rpc);
	manager->rpc = NULL;
fail_sema_release:
	tegra_sema_release(manager->sema);
	manager->sema = NULL;
fail_node_free:
	kfree(node);

out:
	mutex_unlock(&mediasrv->lock);
	return ret;
}

static int mediasrv_release(struct inode *inode, struct file *file)
{
	struct tegra_mediasrv_info *mediasrv = mediasrv_info;
	struct tegra_mediasrv_node *node = file->private_data;
	struct tegra_mediasrv_block *block;
	struct list_head *entry;
	struct list_head *temp;
	u32 message[2];
	int ret;

	mutex_lock(&mediasrv->lock);

	list_for_each_safe(entry, temp, &node->blocks) {
		block = list_entry(entry, struct tegra_mediasrv_block, entry);

		pr_debug("Improperly closed block found!");
		pr_debug("  NVMM Block Handle: 0x%08x\n",
			 block->block.nvmm_block_handle);
		pr_debug("  AVP Block Handle: 0x%08x\n",
			 block->block.avp_block_handle);

		message[0] = 1; /* NvmmManagerMsgType_AbnormalTerm */
		message[1] = block->block.avp_block_handle;

		ret = tegra_rpc_write(mediasrv->manager.rpc, (u8 *)message,
				      sizeof(u32)*2);
		pr_debug("Abnormal termination message result: %d\n", ret);

		if (block->block.avp_block_library_handle) {
			ret = tegra_avp_unload_lib(
				block->block.avp_block_library_handle);
			pr_debug("Unload block (0x%08x) result: %d\n",
				 block->block.avp_block_library_handle, ret);
		}

		if (block->block.service_library_handle) {
			ret = tegra_avp_unload_lib(
				block->block.service_library_handle);
			pr_debug("Unload service (0x%08x) result: %d\n",
				 block->block.service_library_handle, ret);
		}

		mediasrv->nr_blocks--;
		list_del(entry);
		kfree(block);
	}

	mediasrv->nr_iram_shared -= node->nr_iram_shared;
	if (mediasrv->iram.rm_handle && !mediasrv->nr_iram_shared) {
		pr_debug("Improperly freed shared iram found!");
		nvmap_unpin_ids(mediasrv->nvmap, 1, &mediasrv->iram.rm_handle);
		nvmap_free_handle_id(mediasrv->nvmap, mediasrv->iram.rm_handle);
		mediasrv->iram.rm_handle = 0;
		mediasrv->iram.physical_address = 0;
	}

	kfree(node);
	mediasrv->nr_nodes--;
	if (!mediasrv->nr_nodes) {
		tegra_avp_unload_lib(mediasrv->manager.lib.handle);
		mediasrv->manager.lib.handle = 0;

		tegra_avp_release();

		tegra_rpc_release(mediasrv->manager.rpc);
		mediasrv->manager.rpc = NULL;

		tegra_sema_release(mediasrv->manager.sema);
		mediasrv->manager.sema = NULL;
	}

	mutex_unlock(&mediasrv->lock);
	return 0;
}

static int mediasrv_alloc_shared_iram(struct tegra_mediasrv_node *node,
				      union tegra_mediaserver_alloc_info *in,
				      union tegra_mediaserver_alloc_info *out)
{
	struct tegra_mediasrv_info *mediasrv = node->mediasrv;
	size_t align, size;
	struct nvmap_handle_ref *r = NULL;
	unsigned long id, physical_address;
	int ret;

	if (mediasrv->nr_iram_shared) {
		ret = -EBUSY;
		goto iram_shared_done;
	}

	size = PAGE_ALIGN(in->in.u.iram.size);
	r = nvmap_create_handle(mediasrv->nvmap, size);
	if (r < 0) {
		ret = -ENOMEM;
		goto iram_shared_handle_fail;
	}

	id = nvmap_ref_to_id(r);

	align = max_t(size_t, in->in.u.iram.alignment, PAGE_SIZE);
	ret = nvmap_alloc_handle_id(mediasrv->nvmap, id,
				    NVMAP_HEAP_CARVEOUT_IRAM, align,
				    NVMAP_HANDLE_WRITE_COMBINE);
	if (ret < 0)
		goto iram_shared_alloc_fail;

	physical_address = nvmap_pin_ids(mediasrv->nvmap, 1, &id);
	if (physical_address < 0)
		goto iram_shared_pin_fail;

	mediasrv->iram.rm_handle = id;
	mediasrv->iram.physical_address = physical_address;
	goto iram_shared_done;

iram_shared_pin_fail:
	ret = physical_address;
iram_shared_alloc_fail:
	nvmap_free_handle_id(mediasrv->nvmap, id);
iram_shared_handle_fail:
	goto out;

iram_shared_done:
	out->out.u.iram.rm_handle = mediasrv->iram.rm_handle;
	out->out.u.iram.physical_address = mediasrv->iram.physical_address;
	mediasrv->nr_iram_shared++;
	node->nr_iram_shared++;

out:
	return ret;
}

static int mediasrv_alloc(struct tegra_mediasrv_node *node,
			  union tegra_mediaserver_alloc_info *in,
			  union tegra_mediaserver_alloc_info *out)
{
	struct tegra_mediasrv_info *mediasrv = node->mediasrv;
	struct tegra_mediasrv_block *block;
	int ret;

	switch (in->in.tegra_mediaserver_resource_type) {
	case TEGRA_MEDIASERVER_RESOURCE_BLOCK:
		block = kzalloc(sizeof(struct tegra_mediasrv_node), GFP_KERNEL);
		if (!block) {
			ret = -ENOMEM;
			goto fail;
		}

		block->block = in->in.u.block;
		list_add(&block->entry, &node->blocks);
		mediasrv->nr_blocks++;
		out->out.u.block.count = mediasrv->nr_blocks;
		break;

	case TEGRA_MEDIASERVER_RESOURCE_IRAM:
		if (in->in.u.iram.tegra_mediaserver_iram_type ==
		    TEGRA_MEDIASERVER_IRAM_SHARED) {
			ret = mediasrv_alloc_shared_iram(node, in, out);
			if (ret < 0)
				goto fail;
		} else if (in->in.u.iram.tegra_mediaserver_iram_type ==
			   TEGRA_MEDIASERVER_IRAM_SCRATCH) {
			ret = -EINVAL;
			goto fail;
		}
		break;

	default:
		ret = -EINVAL;
		goto fail;
		break;
	}

	return 0;

fail:
	return ret;
}

static void mediasrv_free_shared_iram(struct tegra_mediasrv_node *node)
{
	struct tegra_mediasrv_info *mediasrv = node->mediasrv;

	node->nr_iram_shared--;
	mediasrv->nr_iram_shared--;

	if (!mediasrv->nr_iram_shared) {
		nvmap_unpin_ids(mediasrv->nvmap, 1,
				&mediasrv->iram.rm_handle);
		nvmap_free_handle_id(mediasrv->nvmap,
				     mediasrv->iram.rm_handle);
		mediasrv->iram.rm_handle = 0;
		mediasrv->iram.physical_address = 0;
	}
}

static void mediasrv_free(struct tegra_mediasrv_node *node,
			  union tegra_mediaserver_free_info *in)
{
	struct tegra_mediasrv_info *mediasrv = node->mediasrv;
	struct tegra_mediasrv_block *block;
	struct tegra_mediasrv_block *temp;
	struct list_head *entry;

	switch (in->in.tegra_mediaserver_resource_type) {
	case TEGRA_MEDIASERVER_RESOURCE_BLOCK:
		/* find the specified block in the block list */
		list_for_each(entry, &node->blocks) {
			temp = list_entry(entry, struct tegra_mediasrv_block,
					  entry);
			if (temp->block.nvmm_block_handle !=
			    in->in.u.nvmm_block_handle)
				continue;

			block = temp;
			break;
		}

		if (!block)
			goto done;
		list_del(&block->entry);
		kfree(block);
		break;

	case TEGRA_MEDIASERVER_RESOURCE_IRAM:
		if (in->in.u.iram_rm_handle == mediasrv->iram.rm_handle &&
		    node->nr_iram_shared)
			mediasrv_free_shared_iram(node);
		else
			goto done;
		break;
	}

done:
	return;
}

static int mediasrv_update_block_info(
	struct tegra_mediasrv_node *node,
	union tegra_mediaserver_update_block_info *in)
{
	struct tegra_mediasrv_block *entry;
	struct tegra_mediasrv_block *block;
	int ret;

	list_for_each_entry(entry, &node->blocks, entry) {
		if (entry->block.nvmm_block_handle != in->in.nvmm_block_handle)
			continue;

		block = entry;
		break;
	}

	if (!block)
		goto fail;

	block->block = in->in;
	return 0;

fail:
	ret = -EINVAL;
	return ret;
}

static long mediasrv_unlocked_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	struct tegra_mediasrv_info *mediasrv = mediasrv_info;
	struct tegra_mediasrv_node *node = file->private_data;
	union tegra_mediaserver_alloc_info alloc_in, alloc_out;
	union tegra_mediaserver_free_info free_in;
	union tegra_mediaserver_update_block_info update_in;
	int ret = -ENODEV;

	mutex_lock(&mediasrv->lock);

	switch (cmd) {
	case TEGRA_MEDIASERVER_IOCTL_ALLOC:
		ret = copy_from_user(&alloc_in, (void __user *)arg,
				     sizeof(alloc_in));
		if (ret < 0)
			goto copy_fail;
		ret = mediasrv_alloc(node, &alloc_in, &alloc_out);
		if (ret < 0)
			goto fail;
		ret = copy_to_user((void __user *)arg, &alloc_out,
				   sizeof(alloc_out));
		if (ret < 0)
			goto copy_fail;
		break;

	case TEGRA_MEDIASERVER_IOCTL_FREE:
		ret = copy_from_user(&free_in, (void __user *)arg,
				     sizeof(free_in));
		if (ret < 0)
			goto copy_fail;
		mediasrv_free(node, &free_in);
		break;

	case TEGRA_MEDIASERVER_IOCTL_UPDATE_BLOCK_INFO:
		ret = copy_from_user(&update_in, (void __user *)arg,
				     sizeof(update_in));
		if (ret < 0)
			goto copy_fail;
		ret = mediasrv_update_block_info(node, &update_in);
		if (ret < 0)
			goto fail;
		break;

	default:
		ret = -ENODEV;
		goto fail;
		break;
	}

	mutex_unlock(&mediasrv->lock);
	return 0;

copy_fail:
	ret = -EFAULT;
fail:
	mutex_unlock(&mediasrv->lock);
	return ret;
}

/*
 * Kernel structures and entry points
 */
static const struct file_operations mediaserver_fops = {
	.owner			= THIS_MODULE,
	.open			= mediasrv_open,
	.release		= mediasrv_release,
	.unlocked_ioctl		= mediasrv_unlocked_ioctl,
};

static struct miscdevice mediaserver_misc_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "tegra_mediaserver",
	.fops	= &mediaserver_fops,
};

static int __init tegra_mediaserver_init(void)
{
	struct tegra_mediasrv_info *mediasrv;
	int ret = 0;

	if (mediasrv_info)
		goto busy;

	mediasrv = kzalloc(sizeof(struct tegra_mediasrv_info), GFP_KERNEL);
	if (!mediasrv)
		goto alloc_fail;

	mediasrv->nvmap = nvmap_create_client(nvmap_dev, "tegra_mediaserver");
	if (!mediasrv->nvmap)
		goto nvmap_create_fail;

	ret = misc_register(&mediaserver_misc_device);
	if (ret < 0)
		goto register_fail;

	mediasrv->nr_nodes = 0;
	mutex_init(&mediasrv->lock);

	mediasrv_info = mediasrv;
	goto done;

nvmap_create_fail:
	ret = -ENOMEM;
	kfree(mediasrv);
	goto done;

register_fail:
	nvmap_client_put(mediasrv->nvmap);
	kfree(mediasrv);
	goto done;

alloc_fail:
	ret = -ENOMEM;
	goto done;

busy:
	ret = -EBUSY;
	goto done;

done:
	return ret;
}

void __exit tegra_mediaserver_cleanup(void)
{
	struct tegra_mediasrv_info *mediasrv = mediasrv_info;
	int ret;

	ret = misc_deregister(&mediaserver_misc_device);
	if (ret < 0)
		goto fail;

	nvmap_client_put(mediasrv->nvmap);
	kfree(mediasrv);
	mediasrv_info = NULL;

fail:
	return;
}

module_init(tegra_mediaserver_init);
module_exit(tegra_mediaserver_cleanup);
MODULE_AUTHOR("S. Holmes <sholmes@nvidia.com>");
MODULE_LICENSE("GPL");
