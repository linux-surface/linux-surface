/*
 *
 * Intel Integrated Touch Gfx Interface Layer
 * Copyright (c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/intel_ipts_if.h>

#include "ipts.h"
#include "ipts-msg-handler.h"
#include "ipts-state.h"

static void gfx_processing_complete(void *data)
{
	ipts_info_t *ipts = data;

	if (ipts_get_state(ipts) == IPTS_STA_RAW_DATA_STARTED) {
		schedule_work(&ipts->raw_data_work);
		return;
	}

	ipts_dbg(ipts, "not ready to handle gfx event\n");
}

static void notify_gfx_status(u32 status, void *data)
{
	ipts_info_t *ipts = data;

	ipts->gfx_status = status;
	schedule_work(&ipts->gfx_status_work);
}

static int connect_gfx(ipts_info_t *ipts)
{
	int ret = 0;
	intel_ipts_connect_t ipts_connect;

	ipts_connect.if_version = IPTS_INTERFACE_V1;
	ipts_connect.ipts_cb.workload_complete = gfx_processing_complete;
	ipts_connect.ipts_cb.notify_gfx_status = notify_gfx_status;
	ipts_connect.data = (void*)ipts;

	ret = intel_ipts_connect(&ipts_connect);
	if (ret)
		return ret;

	/* TODO: gfx version check */
	ipts->gfx_info.gfx_handle = ipts_connect.gfx_handle;
	ipts->gfx_info.ipts_ops = ipts_connect.ipts_ops;

	return ret;
}

static void disconnect_gfx(ipts_info_t *ipts)
{
	intel_ipts_disconnect(ipts->gfx_info.gfx_handle);
}

#ifdef RUN_DBG_THREAD
#include "../mei/mei_dev.h"

static struct task_struct *dbg_thread;

static void ipts_print_dbg_info(ipts_info_t* ipts)
{
        char fw_sts_str[MEI_FW_STATUS_STR_SZ];
	u32 *db, *head, *tail;
	intel_ipts_wq_info_t* wq_info;

	wq_info = &ipts->resource.wq_info;

	mei_fw_status_str(ipts->cldev->bus, fw_sts_str, MEI_FW_STATUS_STR_SZ);
	pr_info(">> tdt : fw status : %s\n", fw_sts_str);

	db = (u32*)wq_info->db_addr;
	head = (u32*)wq_info->wq_head_addr;
	tail = (u32*)wq_info->wq_tail_addr;
	pr_info(">> == DB s:%x, c:%x ==\n", *db, *(db+1));
	pr_info(">> == WQ h:%u, t:%u ==\n", *head, *tail);
}

static int ipts_dbg_thread(void *data)
{
	ipts_info_t *ipts = (ipts_info_t *)data;

	pr_info(">> start debug thread\n");

	while (!kthread_should_stop()) {
		if (ipts_get_state(ipts) != IPTS_STA_RAW_DATA_STARTED) {
			pr_info("state is not IPTS_STA_RAW_DATA_STARTED : %d\n",
							ipts_get_state(ipts));
			msleep(5000);
			continue;
		}

		ipts_print_dbg_info(ipts);

		msleep(3000);
	}

	return 0;
}
#endif

int ipts_open_gpu(ipts_info_t *ipts)
{
	int ret = 0;

	ret = connect_gfx(ipts);
	if (ret) {
		ipts_dbg(ipts, "cannot connect GPU\n");
		return ret;
	}

	ret = ipts->gfx_info.ipts_ops.get_wq_info(ipts->gfx_info.gfx_handle,
							&ipts->resource.wq_info);
	if (ret) {
		ipts_dbg(ipts, "error in get_wq_info\n");
		return ret;
	}

#ifdef	RUN_DBG_THREAD
	dbg_thread = kthread_run(ipts_dbg_thread, (void *)ipts, "ipts_debug");
#endif

	return 0;
}

void ipts_close_gpu(ipts_info_t *ipts)
{
	disconnect_gfx(ipts);

#ifdef	RUN_DBG_THREAD
	kthread_stop(dbg_thread);
#endif
}

intel_ipts_mapbuffer_t *ipts_map_buffer(ipts_info_t *ipts, u32 size, u32 flags)
{
	intel_ipts_mapbuffer_t *buf;
	u64 handle;
	int ret;

	buf = devm_kzalloc(&ipts->cldev->dev, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->size = size;
	buf->flags = flags;

	handle = ipts->gfx_info.gfx_handle;
	ret = ipts->gfx_info.ipts_ops.map_buffer(handle, buf);
	if (ret) {
		devm_kfree(&ipts->cldev->dev, buf);
		return NULL;
	}

	return buf;
}

void ipts_unmap_buffer(ipts_info_t *ipts, intel_ipts_mapbuffer_t *buf)
{
	u64 handle;
	int ret;

	if (!buf)
		return;

	handle = ipts->gfx_info.gfx_handle;
	ret = ipts->gfx_info.ipts_ops.unmap_buffer(handle, buf->buf_handle);

	devm_kfree(&ipts->cldev->dev, buf);
}
