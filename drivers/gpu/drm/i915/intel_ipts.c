/*
 * Copyright  2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/intel_ipts_if.h>
#include <drm/drmP.h>

#include "i915_drv.h"

#define SUPPORTED_IPTS_INTERFACE_VERSION	1

#define REACQUIRE_DB_THRESHOLD			8
#define DB_LOST_CHECK_STEP1_INTERVAL		2000	/* ms */
#define DB_LOST_CHECK_STEP2_INTERVAL		500	/* ms */

/* intel IPTS ctx for ipts support */
typedef struct intel_ipts {
	struct drm_device *dev;
	struct i915_gem_context *ipts_context;
	intel_ipts_callback_t ipts_clbks;

	/* buffers' list */
	struct {
		spinlock_t       lock;
		struct list_head list;
	} buffers;

	void *data;

	struct delayed_work reacquire_db_work;
	intel_ipts_wq_info_t wq_info;
	u32	old_tail;
	u32	old_head;
	bool	need_reacquire_db;

	bool	connected;
	bool	initialized;
} intel_ipts_t;

intel_ipts_t intel_ipts;

typedef struct intel_ipts_object {
	struct list_head list;
	struct drm_i915_gem_object *gem_obj;
	void	*cpu_addr;
} intel_ipts_object_t;

static intel_ipts_object_t *ipts_object_create(size_t size, u32 flags)
{
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	intel_ipts_object_t *obj = NULL;
	struct drm_i915_gem_object *gem_obj = NULL;
	int ret = 0;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0) {
		ret = -EINVAL;
		goto err_out;
	}

	/* Allocate the new object */
	gem_obj = i915_gem_object_create(dev_priv, size);
	if (gem_obj == NULL) {
		ret = -ENOMEM;
		goto err_out;
	}

	if (flags & IPTS_BUF_FLAG_CONTIGUOUS) {
		ret = i915_gem_object_attach_phys(gem_obj, PAGE_SIZE);
		if (ret) {
			pr_info(">> ipts no contiguous : %d\n", ret);
			goto err_out;
		}
	}

	obj->gem_obj = gem_obj;

	spin_lock(&intel_ipts.buffers.lock);
	list_add_tail(&obj->list, &intel_ipts.buffers.list);
	spin_unlock(&intel_ipts.buffers.lock);

	return obj;

err_out:
	if (gem_obj)
		i915_gem_free_object(&gem_obj->base);

	if (obj)
		kfree(obj);

	return NULL;
}

static void ipts_object_free(intel_ipts_object_t* obj)
{
	spin_lock(&intel_ipts.buffers.lock);
	list_del(&obj->list);
	spin_unlock(&intel_ipts.buffers.lock);

	i915_gem_free_object(&obj->gem_obj->base);
	kfree(obj);
}

static int ipts_object_pin(intel_ipts_object_t* obj,
					struct i915_gem_context *ipts_ctx)
{
	struct i915_address_space *vm = NULL;
	struct i915_vma *vma = NULL;
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	int ret = 0;

	if (ipts_ctx->ppgtt) {
		vm = &ipts_ctx->ppgtt->base;
	} else {
		vm = &dev_priv->ggtt.base;
	}

	vma = i915_vma_instance(obj->gem_obj, vm, NULL);
	if (IS_ERR(vma)) {
		DRM_ERROR("cannot find or create vma\n");
		return -1;
	}

	ret = i915_vma_pin(vma, 0, PAGE_SIZE, PIN_USER);

	return ret;
}

static void ipts_object_unpin(intel_ipts_object_t *obj)
{
	/* TBD: Add support */
}

static void* ipts_object_map(intel_ipts_object_t *obj)
{

	return i915_gem_object_pin_map(obj->gem_obj, I915_MAP_WB);
}

static void ipts_object_unmap(intel_ipts_object_t* obj)
{
	i915_gem_object_unpin_map(obj->gem_obj);
	obj->cpu_addr = NULL;
}

static int create_ipts_context(void)
{
	struct i915_gem_context *ipts_ctx = NULL;
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	int ret = 0;

	/* Initialize the context right away.*/
	ret = i915_mutex_lock_interruptible(intel_ipts.dev);
	if (ret) {
		DRM_ERROR("i915_mutex_lock_interruptible failed \n");
		return ret;
	}

	ipts_ctx = i915_gem_context_create_ipts(intel_ipts.dev);
	if (IS_ERR(ipts_ctx)) {
		DRM_ERROR("Failed to create IPTS context (error %ld)\n",
			  PTR_ERR(ipts_ctx));
		ret = PTR_ERR(ipts_ctx);
		goto err_unlock;
	}

	ret = execlists_context_deferred_alloc(ipts_ctx, dev_priv->engine[RCS]);
	if (ret) {
		DRM_DEBUG("lr context allocation failed : %d\n", ret);
		goto err_ctx;
	}

	ret = execlists_context_pin(dev_priv->engine[RCS], ipts_ctx);
	if (ret) {
		DRM_DEBUG("lr context pinning failed : %d\n", ret);
		goto err_ctx;
	}

	/* Release the mutex */
	mutex_unlock(&intel_ipts.dev->struct_mutex);

	spin_lock_init(&intel_ipts.buffers.lock);
	INIT_LIST_HEAD(&intel_ipts.buffers.list);

	intel_ipts.ipts_context = ipts_ctx;

	return 0;

err_ctx:
	if (ipts_ctx)
		i915_gem_context_put(ipts_ctx);

err_unlock:
	mutex_unlock(&intel_ipts.dev->struct_mutex);

	return ret;
}

static void destroy_ipts_context(void)
{
	struct i915_gem_context *ipts_ctx = NULL;
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	int ret = 0;

	ipts_ctx = intel_ipts.ipts_context;

	/* Initialize the context right away.*/
	ret = i915_mutex_lock_interruptible(intel_ipts.dev);
	if (ret) {
		DRM_ERROR("i915_mutex_lock_interruptible failed \n");
		return;
	}

	execlists_context_unpin(dev_priv->engine[RCS], ipts_ctx);
	i915_gem_context_put(ipts_ctx);

	mutex_unlock(&intel_ipts.dev->struct_mutex);
}

int intel_ipts_notify_complete(void)
{
	if (intel_ipts.ipts_clbks.workload_complete)
		intel_ipts.ipts_clbks.workload_complete(intel_ipts.data);

	return 0;
}

int intel_ipts_notify_backlight_status(bool backlight_on)
{
	if (intel_ipts.ipts_clbks.notify_gfx_status) {
		if (backlight_on) {
			intel_ipts.ipts_clbks.notify_gfx_status(
						IPTS_NOTIFY_STA_BACKLIGHT_ON,
						intel_ipts.data);
			schedule_delayed_work(&intel_ipts.reacquire_db_work,
				msecs_to_jiffies(DB_LOST_CHECK_STEP1_INTERVAL));
		} else {
			intel_ipts.ipts_clbks.notify_gfx_status(
						IPTS_NOTIFY_STA_BACKLIGHT_OFF,
						intel_ipts.data);
			cancel_delayed_work(&intel_ipts.reacquire_db_work);
		}
	}

	return 0;
}

static void intel_ipts_reacquire_db(intel_ipts_t *intel_ipts_p)
{
	int ret = 0;

	ret = i915_mutex_lock_interruptible(intel_ipts_p->dev);
	if (ret) {
		DRM_ERROR("i915_mutex_lock_interruptible failed \n");
		return;
	}

	/* Reacquire the doorbell */
	i915_guc_ipts_reacquire_doorbell(intel_ipts_p->dev->dev_private);

	mutex_unlock(&intel_ipts_p->dev->struct_mutex);

	return;
}

static int intel_ipts_get_wq_info(uint64_t gfx_handle,
						intel_ipts_wq_info_t *wq_info)
{
	if (gfx_handle != (uint64_t)&intel_ipts) {
		DRM_ERROR("invalid gfx handle\n");
		return -EINVAL;
	}

	*wq_info = intel_ipts.wq_info;

	intel_ipts_reacquire_db(&intel_ipts);
	schedule_delayed_work(&intel_ipts.reacquire_db_work,
				msecs_to_jiffies(DB_LOST_CHECK_STEP1_INTERVAL));

	return 0;
}

static int set_wq_info(void)
{
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_guc_client *client;
	struct guc_process_desc *desc;
	void *base = NULL;
	intel_ipts_wq_info_t *wq_info;
	u64 phy_base = 0;

	wq_info = &intel_ipts.wq_info;

	client = guc->ipts_client;
	if (!client) {
		DRM_ERROR("IPTS GuC client is NOT available\n");
		return -EINVAL;
	}

	base = client->vaddr;
	desc = (struct guc_process_desc *)((u64)base + client->proc_desc_offset);

	desc->wq_base_addr = (u64)base + client->wq_offset;
	desc->db_base_addr = (u64)base + client->doorbell_offset;

	/* IPTS expects physical addresses to pass it to ME */
	phy_base = sg_dma_address(client->vma->pages->sgl);

	wq_info->db_addr = desc->db_base_addr;
        wq_info->db_phy_addr = phy_base + client->doorbell_offset;
        wq_info->db_cookie_offset = offsetof(struct guc_doorbell_info, cookie);
        wq_info->wq_addr = desc->wq_base_addr;
        wq_info->wq_phy_addr = phy_base + client->wq_offset;
        wq_info->wq_head_addr = (u64)&desc->head;
        wq_info->wq_head_phy_addr = phy_base + client->proc_desc_offset +
					offsetof(struct guc_process_desc, head);
        wq_info->wq_tail_addr = (u64)&desc->tail;
        wq_info->wq_tail_phy_addr = phy_base + client->proc_desc_offset +
					offsetof(struct guc_process_desc, tail);
        wq_info->wq_size = desc->wq_size_bytes;

	return 0;
}

static int intel_ipts_init_wq(void)
{
	int ret = 0;

	ret = i915_mutex_lock_interruptible(intel_ipts.dev);
	if (ret) {
		DRM_ERROR("i915_mutex_lock_interruptible failed\n");
		return ret;
	}

	/* disable IPTS submission */
	i915_guc_ipts_submission_disable(intel_ipts.dev->dev_private);

	/* enable IPTS submission */
	ret = i915_guc_ipts_submission_enable(intel_ipts.dev->dev_private,
							intel_ipts.ipts_context);
	if (ret) {
		DRM_ERROR("i915_guc_ipts_submission_enable failed : %d\n", ret);
		goto out;
        }

	ret = set_wq_info();
	if (ret) {
		DRM_ERROR("set_wq_info failed\n");
		goto out;
	}

out:
	mutex_unlock(&intel_ipts.dev->struct_mutex);

	return ret;
}

static void intel_ipts_release_wq(void)
{
	int ret = 0;

	ret = i915_mutex_lock_interruptible(intel_ipts.dev);
	if (ret) {
		DRM_ERROR("i915_mutex_lock_interruptible failed\n");
		return;
	}

	/* disable IPTS submission */
	i915_guc_ipts_submission_disable(intel_ipts.dev->dev_private);

	mutex_unlock(&intel_ipts.dev->struct_mutex);
}

static int intel_ipts_map_buffer(u64 gfx_handle, intel_ipts_mapbuffer_t *mapbuf)
{
	intel_ipts_object_t* obj;
	struct i915_gem_context *ipts_ctx = NULL;
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	struct i915_address_space *vm = NULL;
	struct i915_vma *vma = NULL;
	int ret = 0;

	if (gfx_handle != (uint64_t)&intel_ipts) {
		DRM_ERROR("invalid gfx handle\n");
		return -EINVAL;
	}

	/* Acquire mutex first */
	ret = i915_mutex_lock_interruptible(intel_ipts.dev);
	if (ret) {
		DRM_ERROR("i915_mutex_lock_interruptible failed \n");
		return -EINVAL;
	}

	obj = ipts_object_create(mapbuf->size, mapbuf->flags);
	if (!obj)
		return -ENOMEM;

	ipts_ctx = intel_ipts.ipts_context;
	ret = ipts_object_pin(obj, ipts_ctx);
	if (ret) {
		DRM_ERROR("Not able to pin iTouch obj\n");
		ipts_object_free(obj);
		mutex_unlock(&intel_ipts.dev->struct_mutex);
		return -ENOMEM;
	}

	if (mapbuf->flags & IPTS_BUF_FLAG_CONTIGUOUS) {
		obj->cpu_addr = obj->gem_obj->phys_handle->vaddr;
	} else {
		obj->cpu_addr = ipts_object_map(obj);
	}

	if (ipts_ctx->ppgtt) {
		vm = &ipts_ctx->ppgtt->base;
	} else {
		vm = &dev_priv->ggtt.base;
	}

	vma = i915_vma_instance(obj->gem_obj, vm, NULL);
	if (IS_ERR(vma)) {
		DRM_ERROR("cannot find or create vma\n");
		return -EINVAL;
	}

	mapbuf->gfx_addr = (void*)vma->node.start;
	mapbuf->cpu_addr = (void*)obj->cpu_addr;
	mapbuf->buf_handle = (u64)obj;
	if (mapbuf->flags & IPTS_BUF_FLAG_CONTIGUOUS) {
		mapbuf->phy_addr = (u64)obj->gem_obj->phys_handle->busaddr;
	}

	/* Release the mutex */
	mutex_unlock(&intel_ipts.dev->struct_mutex);

	return 0;
}

static int intel_ipts_unmap_buffer(uint64_t gfx_handle, uint64_t buf_handle)
{
	intel_ipts_object_t* obj = (intel_ipts_object_t*)buf_handle;

	if (gfx_handle != (uint64_t)&intel_ipts) {
		DRM_ERROR("invalid gfx handle\n");
		return -EINVAL;
	}

	if (!obj->gem_obj->phys_handle)
		ipts_object_unmap(obj);
	ipts_object_unpin(obj);
	ipts_object_free(obj);

	return 0;
}

int intel_ipts_connect(intel_ipts_connect_t *ipts_connect)
{
	struct drm_i915_private *dev_priv = intel_ipts.dev->dev_private;
	int ret = 0;

	if (!intel_ipts.initialized)
		return -EIO;

	if (ipts_connect && ipts_connect->if_version <=
					SUPPORTED_IPTS_INTERFACE_VERSION) {

		/* return gpu operations for ipts */
		ipts_connect->ipts_ops.get_wq_info = intel_ipts_get_wq_info;
		ipts_connect->ipts_ops.map_buffer = intel_ipts_map_buffer;
		ipts_connect->ipts_ops.unmap_buffer = intel_ipts_unmap_buffer;
		ipts_connect->gfx_version = INTEL_INFO(dev_priv)->gen;
		ipts_connect->gfx_handle = (uint64_t)&intel_ipts;

		/* save callback and data */
		intel_ipts.data = ipts_connect->data;
		intel_ipts.ipts_clbks = ipts_connect->ipts_cb;

		intel_ipts.connected = true;
	} else {
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(intel_ipts_connect);

void intel_ipts_disconnect(uint64_t gfx_handle)
{
	if (!intel_ipts.initialized)
		return;

	if (gfx_handle != (uint64_t)&intel_ipts ||
					intel_ipts.connected == false) {
		DRM_ERROR("invalid gfx handle\n");
		return;
	}

	intel_ipts.data = 0;
	memset(&intel_ipts.ipts_clbks, 0, sizeof(intel_ipts_callback_t));

	intel_ipts.connected = false;
}
EXPORT_SYMBOL_GPL(intel_ipts_disconnect);

static void reacquire_db_work_func(struct work_struct *work)
{
	struct delayed_work *d_work = container_of(work, struct delayed_work,
							work);
	intel_ipts_t *intel_ipts_p = container_of(d_work, intel_ipts_t,
							reacquire_db_work);
	u32 head;
	u32 tail;
	u32 size;
	u32 load;

	head = *(u32*)intel_ipts_p->wq_info.wq_head_addr;
	tail = *(u32*)intel_ipts_p->wq_info.wq_tail_addr;
	size = intel_ipts_p->wq_info.wq_size;

	if (head >= tail)
		load = head - tail;
	else
		load = head + size - tail;

	if (load < REACQUIRE_DB_THRESHOLD) {
		intel_ipts_p->need_reacquire_db = false;
		goto reschedule_work;
	}

	if (intel_ipts_p->need_reacquire_db) {
		if (intel_ipts_p->old_head == head && intel_ipts_p->old_tail == tail)
			intel_ipts_reacquire_db(intel_ipts_p);
		intel_ipts_p->need_reacquire_db = false;
	} else {
		intel_ipts_p->old_head = head;
		intel_ipts_p->old_tail = tail;
		intel_ipts_p->need_reacquire_db = true;

		/* recheck */
		schedule_delayed_work(&intel_ipts_p->reacquire_db_work,
				msecs_to_jiffies(DB_LOST_CHECK_STEP2_INTERVAL));
		return;
	}

reschedule_work:
	schedule_delayed_work(&intel_ipts_p->reacquire_db_work,
				msecs_to_jiffies(DB_LOST_CHECK_STEP1_INTERVAL));
}

/**
 * intel_ipts_init - Initialize ipts support
 * @dev: drm device
 *
 * Setup the required structures for ipts.
 */
int intel_ipts_init(struct drm_device *dev)
{
	int ret = 0;

	intel_ipts.dev = dev;
	INIT_DELAYED_WORK(&intel_ipts.reacquire_db_work, reacquire_db_work_func);

	ret = create_ipts_context();
	if (ret)
		return -ENOMEM;

	ret = intel_ipts_init_wq();
	if (ret)
		return ret;

	intel_ipts.initialized = true;
	DRM_DEBUG_DRIVER("Intel iTouch framework initialized\n");

	return ret;
}

void intel_ipts_cleanup(struct drm_device *dev)
{
	intel_ipts_object_t *obj, *n;

	if (intel_ipts.dev == dev) {
		list_for_each_entry_safe(obj, n, &intel_ipts.buffers.list, list) {
			list_del(&obj->list);

			if (!obj->gem_obj->phys_handle)
				ipts_object_unmap(obj);
			ipts_object_unpin(obj);
			i915_gem_free_object(&obj->gem_obj->base);
			kfree(obj);
		}

		intel_ipts_release_wq();
		destroy_ipts_context();
		cancel_delayed_work(&intel_ipts.reacquire_db_work);
	}
}
