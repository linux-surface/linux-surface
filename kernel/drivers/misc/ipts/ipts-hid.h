/*
 * Intel Precise Touch & Stylus HID definition
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

#ifndef _IPTS_HID_H_
#define	_IPTS_HID_H_

#define	BUS_MEI				0x44

#if 0 /* TODO : we have special report ID. will implement them */
#define WRITE_CHANNEL_REPORT_ID		0xa
#define READ_CHANNEL_REPORT_ID		0xb
#define CONFIG_CHANNEL_REPORT_ID	0xd
#define VENDOR_INFO_REPORT_ID		0xF
#define SINGLE_TOUCH_REPORT_ID		0x40
#endif

int ipts_hid_init(ipts_info_t *ipts);
void ipts_hid_release(ipts_info_t *ipts);
int ipts_handle_hid_data(ipts_info_t *ipts,
			touch_sensor_hid_ready_for_data_rsp_data_t *hid_rsp);

#endif /* _IPTS_HID_H_ */
