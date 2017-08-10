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

#ifndef _IPTS_STATE_H_
#define _IPTS_STATE_H_

/* ipts driver states */
typedef enum ipts_state {
	IPTS_STA_NONE,
	IPTS_STA_INIT,
	IPTS_STA_RESOURCE_READY,
	IPTS_STA_HID_STARTED,
	IPTS_STA_RAW_DATA_STARTED,
	IPTS_STA_STOPPING
} ipts_state_t;

#endif // _IPTS_STATE_H_
