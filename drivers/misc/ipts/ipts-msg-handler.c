#include <linux/mei_cl_bus.h>

#include "ipts.h"
#include "ipts-hid.h"
#include "ipts-resource.h"
#include "ipts-mei-msgs.h"

int ipts_handle_cmd(ipts_info_t *ipts, u32 cmd, void *data, int data_size)
{
	int ret = 0;
	touch_sensor_msg_h2m_t h2m_msg;
	int len = 0;

	memset(&h2m_msg, 0, sizeof(h2m_msg));

	h2m_msg.command_code = cmd;
	len = sizeof(h2m_msg.command_code) + data_size;
	if (data != NULL && data_size != 0)
		memcpy(&h2m_msg.h2m_data, data, data_size); /* copy payload */

	ret = mei_cldev_send(ipts->cldev, (u8*)&h2m_msg, len);
	if (ret < 0) {
		ipts_err(ipts, "mei_cldev_send() error 0x%X:%d\n",
							cmd, ret);
		return ret;
	}

	return 0;
}

int ipts_send_feedback(ipts_info_t *ipts, int buffer_idx, u32 transaction_id)
{
	int ret;
	int cmd_len;
	touch_sensor_feedback_ready_cmd_data_t fb_ready_cmd;

	cmd_len = sizeof(touch_sensor_feedback_ready_cmd_data_t);
	memset(&fb_ready_cmd, 0, cmd_len);

	fb_ready_cmd.feedback_index = buffer_idx;
	fb_ready_cmd.transaction_id = transaction_id;

	ret = ipts_handle_cmd(ipts, TOUCH_SENSOR_FEEDBACK_READY_CMD,
				&fb_ready_cmd, cmd_len);

	return ret;
}

int ipts_send_sensor_quiesce_io_cmd(ipts_info_t *ipts)
{
	int ret;
	int cmd_len;
	touch_sensor_quiesce_io_cmd_data_t quiesce_io_cmd;

	cmd_len = sizeof(touch_sensor_quiesce_io_cmd_data_t);
	memset(&quiesce_io_cmd, 0, cmd_len);

	ret = ipts_handle_cmd(ipts, TOUCH_SENSOR_QUIESCE_IO_CMD,
				&quiesce_io_cmd, cmd_len);

	return ret;
}

int ipts_send_sensor_hid_ready_for_data_cmd(ipts_info_t *ipts)
{
	return ipts_handle_cmd(ipts, TOUCH_SENSOR_HID_READY_FOR_DATA_CMD, NULL, 0);
}

int ipts_send_sensor_clear_mem_window_cmd(ipts_info_t *ipts)
{
	return ipts_handle_cmd(ipts, TOUCH_SENSOR_CLEAR_MEM_WINDOW_CMD, NULL, 0);
}

static int check_validity(touch_sensor_msg_m2h_t *m2h_msg, u32 msg_len)
{
	int ret = 0;
	int valid_msg_len = sizeof(m2h_msg->command_code);
	u32 cmd_code = m2h_msg->command_code;

	switch (cmd_code) {
		case TOUCH_SENSOR_SET_MODE_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_set_mode_rsp_data_t);
			break;
		case TOUCH_SENSOR_SET_MEM_WINDOW_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_set_mem_window_rsp_data_t);
			break;
		case TOUCH_SENSOR_QUIESCE_IO_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_quiesce_io_rsp_data_t);
			break;
		case TOUCH_SENSOR_HID_READY_FOR_DATA_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_hid_ready_for_data_rsp_data_t);
			break;
		case TOUCH_SENSOR_FEEDBACK_READY_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_feedback_ready_rsp_data_t);
			break;
		case TOUCH_SENSOR_CLEAR_MEM_WINDOW_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_clear_mem_window_rsp_data_t);
			break;
		case TOUCH_SENSOR_NOTIFY_DEV_READY_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_notify_dev_ready_rsp_data_t);
			break;
		case TOUCH_SENSOR_SET_POLICIES_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_set_policies_rsp_data_t);
			break;
		case TOUCH_SENSOR_GET_POLICIES_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_get_policies_rsp_data_t);
			break;
		case TOUCH_SENSOR_RESET_RSP:
			valid_msg_len +=
				sizeof(touch_sensor_reset_rsp_data_t);
			break;
	}

	if (valid_msg_len != msg_len) {
		return -EINVAL;
	}

	return ret;
}

int ipts_start(ipts_info_t *ipts)
{
	int ret = 0;
	/* TODO : check if we need to do SET_POLICIES_CMD
	we need to do this when protocol version doesn't match with reported one
	how we keep vendor specific data is the first thing to solve */

	ipts_set_state(ipts, IPTS_STA_INIT);
	ipts->num_of_parallel_data_buffers = TOUCH_SENSOR_MAX_DATA_BUFFERS;

#ifdef ENABLE_IPTS_DEBUG
	ipts->sensor_mode = TOUCH_SENSOR_MODE_HID; /* start with HID */
#endif

	ret = ipts_handle_cmd(ipts, TOUCH_SENSOR_NOTIFY_DEV_READY_CMD, NULL, 0);

	return ret;
}

void ipts_stop(ipts_info_t *ipts)
{
	ipts_state_t old_state;

	old_state = ipts_get_state(ipts);
	ipts_set_state(ipts, IPTS_STA_STOPPING);

	if (old_state < IPTS_STA_RESOURCE_READY)
		return;

	if (old_state == IPTS_STA_RAW_DATA_STARTED ||
					old_state == IPTS_STA_HID_STARTED) {
        	ipts_free_default_resource(ipts);
		ipts_free_raw_data_resource(ipts);

		return;
	}
}

int ipts_restart(ipts_info_t *ipts)
{
	int ret = 0;

	ipts_dbg(ipts, "ipts restart\n");

	ipts_stop(ipts);

	ipts->retry++;
	if (ipts->retry == IPTS_MAX_RETRY && 
			ipts->sensor_mode == TOUCH_SENSOR_MODE_RAW_DATA) {
		/* try with HID mode */
		ipts->sensor_mode = TOUCH_SENSOR_MODE_HID;
	} else if (ipts->retry > IPTS_MAX_RETRY) {
		return -EPERM;
	}

	ipts_send_sensor_quiesce_io_cmd(ipts);
	ipts->restart = true;

	return ret;
}

int ipts_switch_sensor_mode(ipts_info_t *ipts, int new_sensor_mode)
{
	int ret = 0;

        ipts->new_sensor_mode = new_sensor_mode;
	ipts->switch_sensor_mode = true;
        ret = ipts_send_sensor_quiesce_io_cmd(ipts);

	return ret;
}

#define rsp_failed(ipts, cmd, status) ipts_err(ipts, \
				"0x%08x failed status = %d\n", cmd, status);

int ipts_handle_resp(ipts_info_t *ipts, touch_sensor_msg_m2h_t *m2h_msg,
								u32 msg_len)
{
	int ret = 0;
	int rsp_status = 0;
	int cmd_status = 0;
	int cmd_len = 0;
	u32 cmd;

	if (!check_validity(m2h_msg, msg_len)) {
		ipts_err(ipts, "wrong rsp\n");
		return -EINVAL;
	}

	rsp_status = m2h_msg->status;
	cmd = m2h_msg->command_code;

	switch (cmd) {
		case TOUCH_SENSOR_NOTIFY_DEV_READY_RSP:
			if (rsp_status != 0 &&
			  rsp_status != TOUCH_STATUS_SENSOR_FAIL_NONFATAL) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			cmd_status = ipts_handle_cmd(ipts,
					TOUCH_SENSOR_GET_DEVICE_INFO_CMD,
					NULL, 0);
			break;
		case TOUCH_SENSOR_GET_DEVICE_INFO_RSP:
			if (rsp_status != 0 &&
			  rsp_status != TOUCH_STATUS_COMPAT_CHECK_FAIL) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			memcpy(&ipts->device_info,
				&m2h_msg->m2h_data.device_info_rsp_data,
				sizeof(touch_sensor_get_device_info_rsp_data_t));

			/*
			    TODO : support raw_request during HID init.
			    Although HID init happens here, technically most of
			    reports (for both direction) can be issued only
			    after SET_MEM_WINDOWS_CMD since they may require
			    ME or touch IC. If ipts vendor requires raw_request
			    during HID init, we need to consider to move HID init.
			*/
			if (ipts->hid_desc_ready == false) {
				ret = ipts_hid_init(ipts);
				if (ret)
					break;
			}

			cmd_status = ipts_send_sensor_clear_mem_window_cmd(ipts);

			break;
		case TOUCH_SENSOR_CLEAR_MEM_WINDOW_RSP:
		{
			touch_sensor_set_mode_cmd_data_t sensor_mode_cmd;

			if (rsp_status != 0 &&
					rsp_status != TOUCH_STATUS_TIMEOUT) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			/* allocate default resource : common & hid only */
			if (!ipts_is_default_resource_ready(ipts)) {
				ret = ipts_allocate_default_resource(ipts);
				if (ret)
					break;
			}

			if (ipts->sensor_mode == TOUCH_SENSOR_MODE_RAW_DATA &&
					!ipts_is_raw_data_resource_ready(ipts)) {
				ret = ipts_allocate_raw_data_resource(ipts);
				if (ret) {
					ipts_free_default_resource(ipts);
					break;
				}
			}

			ipts_set_state(ipts, IPTS_STA_RESOURCE_READY);

			cmd_len = sizeof(touch_sensor_set_mode_cmd_data_t);
			memset(&sensor_mode_cmd, 0, cmd_len);
			sensor_mode_cmd.sensor_mode = ipts->sensor_mode;
			cmd_status = ipts_handle_cmd(ipts,
				TOUCH_SENSOR_SET_MODE_CMD,
				&sensor_mode_cmd, cmd_len);
			break;
		}
		case TOUCH_SENSOR_SET_MODE_RSP:
		{
			touch_sensor_set_mem_window_cmd_data_t smw_cmd;

			if (rsp_status != 0) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			cmd_len = sizeof(touch_sensor_set_mem_window_cmd_data_t);
			memset(&smw_cmd, 0, cmd_len);
			ipts_get_set_mem_window_cmd_data(ipts, &smw_cmd);
			cmd_status = ipts_handle_cmd(ipts,
				TOUCH_SENSOR_SET_MEM_WINDOW_CMD,
				&smw_cmd, cmd_len);
			break;
		}
		case TOUCH_SENSOR_SET_MEM_WINDOW_RSP:
			if (rsp_status != 0) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			cmd_status = ipts_send_sensor_hid_ready_for_data_cmd(ipts);
			if (cmd_status)
				break;

			if (ipts->sensor_mode == TOUCH_SENSOR_MODE_HID) {
				ipts_set_state(ipts, IPTS_STA_HID_STARTED);
			} else if (ipts->sensor_mode == TOUCH_SENSOR_MODE_RAW_DATA) {
				ipts_set_state(ipts, IPTS_STA_RAW_DATA_STARTED);
			}

			ipts_err(ipts, "touch enabled %d\n", ipts_get_state(ipts));

			break;
		case TOUCH_SENSOR_HID_READY_FOR_DATA_RSP:
		{
			touch_sensor_hid_ready_for_data_rsp_data_t *hid_data;
			ipts_state_t state;

			if (rsp_status != 0 &&
				  rsp_status != TOUCH_STATUS_SENSOR_DISABLED) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			state = ipts_get_state(ipts);
			if (ipts->sensor_mode == TOUCH_SENSOR_MODE_HID &&
						state == IPTS_STA_HID_STARTED) {

				hid_data = &m2h_msg->m2h_data.hid_ready_for_data_rsp_data;

				/* HID mode only uses buffer 0 */
				if (hid_data->touch_data_buffer_index != 0)
					break;

				/* handle hid data */
				ipts_handle_hid_data(ipts, hid_data);
			}

			break;
		}
		case TOUCH_SENSOR_FEEDBACK_READY_RSP:
			if (rsp_status != 0 &&
			  rsp_status != TOUCH_STATUS_COMPAT_CHECK_FAIL) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			if (m2h_msg->m2h_data.feedback_ready_rsp_data.
					feedback_index == TOUCH_HID_2_ME_BUFFER_ID)
				break;

			if (ipts->sensor_mode == TOUCH_SENSOR_MODE_HID) {
				cmd_status = ipts_handle_cmd(ipts,
					TOUCH_SENSOR_HID_READY_FOR_DATA_CMD,
					NULL, 0);
			}

			/* reset retry since we are getting touch data */
			ipts->retry = 0;

			break;
		case TOUCH_SENSOR_QUIESCE_IO_RSP:
		{
			ipts_state_t state;

			if (rsp_status != 0) {
				rsp_failed(ipts, cmd, rsp_status);
				break;
			}

			state = ipts_get_state(ipts);
			if (state == IPTS_STA_STOPPING && ipts->restart) {
				ipts_dbg(ipts, "restart\n");
			        ipts_start(ipts);
				ipts->restart = 0;
				break;
			}

			/* support sysfs debug node for switch sensor mode */
			if (ipts->switch_sensor_mode) {
				ipts_set_state(ipts, IPTS_STA_INIT);
				ipts->sensor_mode = ipts->new_sensor_mode;
				ipts->switch_sensor_mode = false;

				ipts_send_sensor_clear_mem_window_cmd(ipts);
			}

			break;
		}
	}

	/* handle error in rsp_status */
	if (rsp_status != 0) {
		switch (rsp_status) {
			case TOUCH_STATUS_SENSOR_EXPECTED_RESET:
			case TOUCH_STATUS_SENSOR_UNEXPECTED_RESET:
				ipts_dbg(ipts, "sensor reset %d\n", rsp_status);
				ipts_restart(ipts);
				break;
			default:
				ipts_dbg(ipts, "cmd : 0x%08x, status %d\n",
								cmd,
								rsp_status);
				break;
		}
	}

	if (cmd_status) {
		ipts_restart(ipts);
	}

	return ret;
}
