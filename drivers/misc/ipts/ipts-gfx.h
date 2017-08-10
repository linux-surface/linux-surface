/*
 * Intel Precise Touch & Stylus gpu wrapper
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
 */


#ifndef _IPTS_GFX_H_
#define _IPTS_GFX_H_

int ipts_open_gpu(ipts_info_t *ipts);
void ipts_close_gpu(ipts_info_t *ipts);
intel_ipts_mapbuffer_t *ipts_map_buffer(ipts_info_t *ipts, u32 size, u32 flags);
void ipts_unmap_buffer(ipts_info_t *ipts, intel_ipts_mapbuffer_t *buf);

#endif // _IPTS_GFX_H_
