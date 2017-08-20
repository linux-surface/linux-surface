/*
 * Intel Precise Touch & Stylus state codes
 *
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

#ifndef _IPTS_RESOURCE_H_
#define _IPTS_RESOURCE_H_

int ipts_allocate_default_resource(ipts_info_t *ipts);
void ipts_free_default_resource(ipts_info_t *ipts);
int ipts_allocate_raw_data_resource(ipts_info_t *ipts);
void ipts_free_raw_data_resource(ipts_info_t *ipts);
void ipts_get_set_mem_window_cmd_data(ipts_info_t *ipts,
				touch_sensor_set_mem_window_cmd_data_t *data);
void ipts_set_input_buffer(ipts_info_t *ipts, int parallel_idx,
						u8* cpu_addr, u64 dma_addr);
void ipts_set_output_buffer(ipts_info_t *ipts, int parallel_idx, int output_idx,
						u8* cpu_addr, u64 dma_addr);

#endif // _IPTS_RESOURCE_H_
