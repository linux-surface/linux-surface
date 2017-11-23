/*
 *
 * Intel Management Engine Interface (Intel MEI) Client Driver for IPTS
 * Copyright (c) 2016, Intel Corporation.
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

#ifndef _IPTS_H_
#define _IPTS_H_

#include <linux/types.h>
#include <linux/mei_cl_bus.h>
#include <linux/hid.h>
#include <linux/intel_ipts_if.h>

#include "ipts-mei-msgs.h"
#include "ipts-state.h"
#include "ipts-binary-spec.h"

//#define ENABLE_IPTS_DEBUG		/* enable IPTS debug */

#ifdef ENABLE_IPTS_DEBUG

#define ipts_info(ipts, format, arg...) do {\
	dev_info(&ipts->cldev->dev, format, ##arg);\
} while (0)

#define ipts_dbg(ipts, format, arg...) do {\
	dev_info(&ipts->cldev->dev, format, ##arg);\
} while (0)

//#define RUN_DBG_THREAD

#else

#define ipts_info(ipts, format, arg...) do {} while(0);
#define ipts_dbg(ipts, format, arg...) do {} while(0);

#endif

#define ipts_err(ipts, format, arg...) do {\
	dev_err(&ipts->cldev->dev, format, ##arg);\
} while (0)

#define HID_PARALLEL_DATA_BUFFERS	TOUCH_SENSOR_MAX_DATA_BUFFERS

#define IPTS_MAX_RETRY			3

typedef struct ipts_buffer_info {
	char *addr;
	dma_addr_t dma_addr;
} ipts_buffer_info_t;

typedef struct ipts_gfx_info {
	u64     gfx_handle;
	intel_ipts_ops_t ipts_ops;
} ipts_gfx_info_t;

typedef struct ipts_resource {
	/* ME & Gfx resource */
	ipts_buffer_info_t touch_data_buffer_raw[HID_PARALLEL_DATA_BUFFERS];
	ipts_buffer_info_t touch_data_buffer_hid;

	ipts_buffer_info_t feedback_buffer[HID_PARALLEL_DATA_BUFFERS];

	ipts_buffer_info_t hid2me_buffer;
	u32 hid2me_buffer_size;

	u8 wq_item_size;
	intel_ipts_wq_info_t wq_info;

	/* ME2HID buffer */
	char *me2hid_buffer;

	/* Gfx specific resource */
	ipts_buffer_info_t raw_data_mode_output_buffer
	    [HID_PARALLEL_DATA_BUFFERS][MAX_NUM_OUTPUT_BUFFERS];

	int num_of_outputs;

	bool default_resource_ready;
	bool raw_data_resource_ready;
} ipts_resource_t;

typedef struct ipts_info {
	struct mei_cl_device *cldev;
	struct hid_device *hid;

	struct work_struct init_work;
	struct work_struct raw_data_work;
	struct work_struct gfx_status_work;

	struct task_struct *event_loop;

#if IS_ENABLED(CONFIG_DEBUG_FS)
        struct dentry *dbgfs_dir;
#endif

	ipts_state_t	state;

	touch_sensor_mode_t	sensor_mode;
	touch_sensor_get_device_info_rsp_data_t device_info;
	ipts_resource_t	resource;
	u8		hid_input_report[HID_MAX_BUFFER_SIZE];
	int		num_of_parallel_data_buffers;
	bool		hid_desc_ready;

	int current_buffer_index;
	int last_buffer_completed;
	int *last_submitted_id;

	ipts_gfx_info_t gfx_info;
	u64		kernel_handle;
	int             gfx_status;
	bool		display_status;

	bool		switch_sensor_mode;
	touch_sensor_mode_t	new_sensor_mode;

	int		retry;
	bool		restart;
} ipts_info_t;

#if IS_ENABLED(CONFIG_DEBUG_FS)
int ipts_dbgfs_register(ipts_info_t *ipts, const char *name);
void ipts_dbgfs_deregister(ipts_info_t *ipts);
#else
static int ipts_dbgfs_register(ipts_info_t *ipts, const char *name);
static void ipts_dbgfs_deregister(ipts_info_t *ipts);
#endif /* CONFIG_DEBUG_FS */

/* inline functions */
static inline void ipts_set_state(ipts_info_t *ipts, ipts_state_t state)
{
	ipts->state = state;
}

static inline ipts_state_t ipts_get_state(const ipts_info_t *ipts)
{
	return ipts->state;
}

static inline bool ipts_is_default_resource_ready(const ipts_info_t *ipts)
{
	return ipts->resource.default_resource_ready;
}

static inline bool ipts_is_raw_data_resource_ready(const ipts_info_t *ipts)
{
	return ipts->resource.raw_data_resource_ready;
}

static inline ipts_buffer_info_t* ipts_get_feedback_buffer(ipts_info_t *ipts,
								int buffer_idx)
{
	return &ipts->resource.feedback_buffer[buffer_idx];
}

static inline ipts_buffer_info_t* ipts_get_touch_data_buffer_hid(ipts_info_t *ipts)
{
	return &ipts->resource.touch_data_buffer_hid;
}

static inline ipts_buffer_info_t* ipts_get_output_buffers_by_parallel_id(
							ipts_info_t *ipts,
                                                        int parallel_idx)
{
	return &ipts->resource.raw_data_mode_output_buffer[parallel_idx][0];
}

static inline ipts_buffer_info_t* ipts_get_hid2me_buffer(ipts_info_t *ipts)
{
	return &ipts->resource.hid2me_buffer;
}

static inline void ipts_set_wq_item_size(ipts_info_t *ipts, u8 size)
{
	ipts->resource.wq_item_size = size;
}

static inline u8 ipts_get_wq_item_size(const ipts_info_t *ipts)
{
	return ipts->resource.wq_item_size;
}

static inline int ipts_get_num_of_parallel_buffers(const ipts_info_t *ipts)
{
	return ipts->num_of_parallel_data_buffers;
}

#endif // _IPTS_H_
