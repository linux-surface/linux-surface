/*
 *
 * Intel Precise Touch & Stylus ME message handler
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

#ifndef _IPTS_MSG_HANDLER_H
#define _IPTS_MSG_HANDLER_H

int ipts_handle_cmd(ipts_info_t *ipts, u32 cmd, void *data, int data_size);
int ipts_start(ipts_info_t *ipts);
void ipts_stop(ipts_info_t *ipts);
int ipts_switch_sensor_mode(ipts_info_t *ipts, int new_sensor_mode);
int ipts_handle_resp(ipts_info_t *ipts, touch_sensor_msg_m2h_t *m2h_msg,
                        					u32 msg_len);
int ipts_handle_processed_data(ipts_info_t *ipts);
int ipts_send_feedback(ipts_info_t *ipts, int buffer_idx, u32 transaction_id);
int ipts_send_sensor_quiesce_io_cmd(ipts_info_t *ipts);
int ipts_send_sensor_hid_ready_for_data_cmd(ipts_info_t *ipts);
int ipts_send_sensor_clear_mem_window_cmd(ipts_info_t *ipts);

#endif /* _IPTS_MSG_HANDLER_H */
