/*
 *
 * GFX interface to support Intel Precise Touch & Stylus
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

#ifndef INTEL_IPTS_IF_H
#define INTEL_IPTS_IF_H

enum {
	IPTS_INTERFACE_V1 = 1,
};

#define IPTS_BUF_FLAG_CONTIGUOUS	0x01

#define IPTS_NOTIFY_STA_BACKLIGHT_OFF	0x00
#define IPTS_NOTIFY_STA_BACKLIGHT_ON	0x01

typedef struct intel_ipts_mapbuffer {
	u32	size;
	u32	flags;
	void	*gfx_addr;
	void	*cpu_addr;
	u64	buf_handle;
	u64	phy_addr;
} intel_ipts_mapbuffer_t;

typedef struct intel_ipts_wq_info {
	u64 db_addr;
	u64 db_phy_addr;
	u32 db_cookie_offset;
	u32 wq_size;
	u64 wq_addr;
	u64 wq_phy_addr;
	u64 wq_head_addr;	/* head of wq is managed by GPU */
	u64 wq_head_phy_addr;	/* head of wq is managed by GPU */
	u64 wq_tail_addr;	/* tail of wq is managed by CSME */
	u64 wq_tail_phy_addr;	/* tail of wq is managed by CSME */
} intel_ipts_wq_info_t;

typedef struct intel_ipts_ops {
	int (*get_wq_info)(uint64_t gfx_handle, intel_ipts_wq_info_t *wq_info);
	int (*map_buffer)(uint64_t gfx_handle, intel_ipts_mapbuffer_t *mapbuffer);
	int (*unmap_buffer)(uint64_t gfx_handle, uint64_t buf_handle);
} intel_ipts_ops_t;

typedef struct intel_ipts_callback {
        void (*workload_complete)(void *data);
        void (*notify_gfx_status)(u32 status, void *data);
} intel_ipts_callback_t;

typedef struct intel_ipts_connect {
        intel_ipts_callback_t ipts_cb;	/* input : callback addresses */
	void *data;			/* input : callback data */
        u32 if_version;			/* input : interface version */

        u32 gfx_version;		/* output : gfx version */
        u64 gfx_handle;			/* output : gfx handle */
	intel_ipts_ops_t ipts_ops;	/* output : gfx ops for IPTS */
} intel_ipts_connect_t;

int intel_ipts_connect(intel_ipts_connect_t *ipts_connect);
void intel_ipts_disconnect(uint64_t gfx_handle);

#endif // INTEL_IPTS_IF_H
