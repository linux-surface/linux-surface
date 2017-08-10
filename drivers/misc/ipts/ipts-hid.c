/*
 * Intel Precise Touch & Stylus HID driver
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

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/hid.h>
#include <linux/vmalloc.h>

#include "ipts.h"
#include "ipts-resource.h"
#include "ipts-sensor-regs.h"
#include "ipts-msg-handler.h"

#define BUS_MEI				0x44

#define	HID_DESC_INTEL	"intel/ipts/intel_desc.bin"
#define	HID_DESC_VENDOR	"intel/ipts/vendor_desc.bin"
MODULE_FIRMWARE(HID_DESC_INTEL);
MODULE_FIRMWARE(HID_DESC_VENDOR);

typedef enum output_buffer_payload_type {
	OUTPUT_BUFFER_PAYLOAD_ERROR = 0,
	OUTPUT_BUFFER_PAYLOAD_HID_INPUT_REPORT,
	OUTPUT_BUFFER_PAYLOAD_HID_FEATURE_REPORT,
	OUTPUT_BUFFER_PAYLOAD_KERNEL_LOAD,
	OUTPUT_BUFFER_PAYLOAD_FEEDBACK_BUFFER
} output_buffer_payload_type_t;

typedef struct kernel_output_buffer_header {
	u16 length;
	u8 payload_type;
	u8 reserved1;
	touch_hid_private_data_t hid_private_data;
	u8 reserved2[28];
	u8 data[0];
} kernel_output_buffer_header_t;

typedef struct kernel_output_payload_error {
	u16 severity;
	u16 source;
	u8 code[4];
	char string[128];
} kernel_output_payload_error_t;

static int ipts_hid_get_hid_descriptor(ipts_info_t *ipts, u8 **desc, int *size)
{
	u8 *buf;
	int hid_size = 0, ret = 0;
	const struct firmware *intel_desc = NULL;
	const struct firmware *vendor_desc = NULL;
	const char *intel_desc_path = HID_DESC_INTEL;
	const char *vendor_desc_path = HID_DESC_VENDOR;

	ret = request_firmware(&intel_desc, intel_desc_path, &ipts->cldev->dev);
	if (ret) {
		goto no_hid;
	}
	hid_size = intel_desc->size;

	ret = request_firmware(&vendor_desc, vendor_desc_path, &ipts->cldev->dev);
	if (ret) {
		ipts_dbg(ipts, "error in reading HID Vendor Descriptor\n");
	} else {
		hid_size += vendor_desc->size;
	}

	ipts_dbg(ipts, "hid size = %d\n", hid_size);
	buf = vmalloc(hid_size);
	if (buf == NULL) {
		ret = -ENOMEM;
		goto no_mem;
	}

	memcpy(buf, intel_desc->data, intel_desc->size);
	if (vendor_desc) {
		memcpy(&buf[intel_desc->size], vendor_desc->data,
							vendor_desc->size);
		release_firmware(vendor_desc);
	}

	release_firmware(intel_desc);

	*desc = buf;
	*size = hid_size;

	return 0;
no_mem :
	if (vendor_desc)
		release_firmware(vendor_desc);
	release_firmware(intel_desc);

no_hid :
	return ret;
}

static int ipts_hid_parse(struct hid_device *hid)
{
	ipts_info_t *ipts = hid->driver_data;
	int ret = 0, size;
	u8 *buf;

	ipts_dbg(ipts, "ipts_hid_parse() start\n");
	ret = ipts_hid_get_hid_descriptor(ipts, &buf, &size);
	if (ret != 0) {
		ipts_dbg(ipts, "ipts_hid_ipts_get_hid_descriptor ret %d\n", ret);
		return -EIO;
	}

	ret = hid_parse_report(hid, buf, size);
	vfree(buf);
	if (ret) {
		ipts_err(ipts, "hid_parse_report error : %d\n", ret);
		goto out;
	}

	ipts->hid_desc_ready = true;
out:
	return ret;
}

static int ipts_hid_start(struct hid_device *hid)
{
	return 0;
}

static void ipts_hid_stop(struct hid_device *hid)
{
	return;
}

static int ipts_hid_open(struct hid_device *hid)
{
	return 0;
}

static void ipts_hid_close(struct hid_device *hid)
{
	ipts_info_t *ipts = hid->driver_data;

	ipts->hid_desc_ready = false;

	return;
}

static int ipts_hid_send_hid2me_feedback(ipts_info_t *ipts, u32 fb_data_type,
							__u8 *buf, size_t count)
{
	ipts_buffer_info_t *fb_buf;
	touch_feedback_hdr_t *feedback;
	u8 *payload;
	int header_size;
	ipts_state_t state;

	header_size = sizeof(touch_feedback_hdr_t);

	if (count > ipts->resource.hid2me_buffer_size - header_size)
		return -EINVAL;

	state = ipts_get_state(ipts);
	if (state != IPTS_STA_RAW_DATA_STARTED && state != IPTS_STA_HID_STARTED)
		return 0;

	fb_buf = ipts_get_hid2me_buffer(ipts);
	feedback = (touch_feedback_hdr_t *)fb_buf->addr;
	payload = fb_buf->addr + header_size;
	memset(feedback, 0, header_size);

	feedback->feedback_data_type = fb_data_type;
	feedback->feedback_cmd_type = TOUCH_FEEDBACK_CMD_TYPE_NONE;
	feedback->payload_size_bytes = count;
	feedback->buffer_id = TOUCH_HID_2_ME_BUFFER_ID;
	feedback->protocol_ver = 0;
	feedback->reserved[0] = 0xAC;

	/* copy payload */
	memcpy(payload, buf, count);

	ipts_send_feedback(ipts, TOUCH_HID_2_ME_BUFFER_ID, 0);

	return 0;
}

static int ipts_hid_raw_request(struct hid_device *hid,
				unsigned char report_number, __u8 *buf,
				size_t count, unsigned char report_type,
				int reqtype)
{
	ipts_info_t *ipts = hid->driver_data;
	u32 fb_data_type;

	ipts_dbg(ipts, "hid raw request => report %d, request %d\n",
						 (int)report_type, reqtype);

	if (report_type != HID_FEATURE_REPORT)
		return 0;

	switch (reqtype) {
		case HID_REQ_GET_REPORT:
			fb_data_type = TOUCH_FEEDBACK_DATA_TYPE_GET_FEATURES;
			break;
		case HID_REQ_SET_REPORT:
			fb_data_type = TOUCH_FEEDBACK_DATA_TYPE_SET_FEATURES;
			break;
		default:
			ipts_err(ipts, "raw request not supprted: %d\n", reqtype);
			return -EIO;
	}

	return ipts_hid_send_hid2me_feedback(ipts, fb_data_type, buf, count);
}

static int ipts_hid_output_report(struct hid_device *hid,
					__u8 *buf, size_t count)
{
	ipts_info_t *ipts = hid->driver_data;
	u32 fb_data_type;

	ipts_dbg(ipts, "hid output report\n");

	fb_data_type = TOUCH_FEEDBACK_DATA_TYPE_OUTPUT_REPORT;

	return ipts_hid_send_hid2me_feedback(ipts, fb_data_type, buf, count);
}

static struct hid_ll_driver ipts_hid_ll_driver = {
	.parse = ipts_hid_parse,
	.start = ipts_hid_start,
	.stop = ipts_hid_stop,
	.open = ipts_hid_open,
	.close = ipts_hid_close,
	.raw_request = ipts_hid_raw_request,
	.output_report = ipts_hid_output_report,
};

int ipts_hid_init(ipts_info_t *ipts)
{
	int ret = 0;
	struct hid_device *hid;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto err_dev;
	}

	hid->driver_data = ipts;
	hid->ll_driver = &ipts_hid_ll_driver;
	hid->dev.parent = &ipts->cldev->dev;
	hid->bus = BUS_MEI;
	hid->version = ipts->device_info.fw_rev;
	hid->vendor = ipts->device_info.vendor_id;
	hid->product = ipts->device_info.device_id;

	snprintf(hid->phys, sizeof(hid->phys), "heci3");
	snprintf(hid->name, sizeof(hid->name),
		 "%s %04hX:%04hX", "ipts", hid->vendor, hid->product);

	ret = hid_add_device(hid);
	if (ret) {
		if (ret != -ENODEV)
			ipts_err(ipts, "can't add hid device: %d\n", ret);
		goto err_mem_free;
	}

	ipts->hid = hid;

	return 0;

err_mem_free:
	hid_destroy_device(hid);
err_dev:
	return ret;
}

void ipts_hid_release(ipts_info_t *ipts)
{
	struct hid_device *hid = ipts->hid;
	hid_destroy_device(hid);
}

int ipts_handle_hid_data(ipts_info_t *ipts,
			touch_sensor_hid_ready_for_data_rsp_data_t *hid_rsp)
{
	touch_raw_data_hdr_t *raw_header;
	ipts_buffer_info_t *buffer_info;
	touch_feedback_hdr_t *feedback;
	u8 *raw_data;
	int touch_data_buffer_index;
	int transaction_id;
	int ret = 0;

	touch_data_buffer_index = (int)hid_rsp->touch_data_buffer_index;
	buffer_info = ipts_get_touch_data_buffer_hid(ipts);
	raw_header = (touch_raw_data_hdr_t *)buffer_info->addr;
	transaction_id = raw_header->hid_private_data.transaction_id;

	raw_data = (u8*)raw_header + sizeof(touch_raw_data_hdr_t);
	if (raw_header->data_type == TOUCH_RAW_DATA_TYPE_HID_REPORT) {
		memcpy(ipts->hid_input_report, raw_data,
				raw_header->raw_data_size_bytes);

		ret = hid_input_report(ipts->hid, HID_INPUT_REPORT,
					(u8*)ipts->hid_input_report,
					raw_header->raw_data_size_bytes, 1);
		if (ret) {
			ipts_err(ipts, "error in hid_input_report : %d\n", ret);
		}
	} else if (raw_header->data_type == TOUCH_RAW_DATA_TYPE_GET_FEATURES) {
		/* TODO: implement together with "get feature ioctl" */
	} else if (raw_header->data_type == TOUCH_RAW_DATA_TYPE_ERROR) {
		touch_error_t *touch_err = (touch_error_t *)raw_data;

		ipts_err(ipts, "error type : %d, me fw error : %x, err reg : %x\n",
				touch_err->touch_error_type,
				touch_err->touch_me_fw_error.value,
				touch_err->touch_error_register.reg_value);
	}

	/* send feedback data for HID mode */
        buffer_info = ipts_get_feedback_buffer(ipts, touch_data_buffer_index);
	feedback = (touch_feedback_hdr_t *)buffer_info->addr;
	memset(feedback, 0, sizeof(touch_feedback_hdr_t));
	feedback->feedback_cmd_type = TOUCH_FEEDBACK_CMD_TYPE_NONE;
	feedback->payload_size_bytes = 0;
	feedback->buffer_id = touch_data_buffer_index;
	feedback->protocol_ver = 0;
	feedback->reserved[0] = 0xAC;

	ret = ipts_send_feedback(ipts, touch_data_buffer_index, transaction_id);

	return ret;
}

static int handle_outputs(ipts_info_t *ipts, int parallel_idx)
{
	kernel_output_buffer_header_t *out_buf_hdr;
	ipts_buffer_info_t *output_buf, *fb_buf = NULL;
	u8 *input_report, *payload;
	u32 transaction_id;
	int i, payload_size, ret = 0, header_size;

	header_size = sizeof(kernel_output_buffer_header_t);
	output_buf = ipts_get_output_buffers_by_parallel_id(ipts, parallel_idx);
	for (i = 0; i < ipts->resource.num_of_outputs; i++) {
		out_buf_hdr = (kernel_output_buffer_header_t*)output_buf[i].addr;
		if (out_buf_hdr->length < header_size)
			continue;

		payload_size = out_buf_hdr->length - header_size;
		payload = out_buf_hdr->data;

		switch(out_buf_hdr->payload_type) {
			case OUTPUT_BUFFER_PAYLOAD_HID_INPUT_REPORT:
				input_report = ipts->hid_input_report;
				memcpy(input_report, payload, payload_size);
				hid_input_report(ipts->hid, HID_INPUT_REPORT,
						input_report, payload_size, 1);
				break;
			case OUTPUT_BUFFER_PAYLOAD_HID_FEATURE_REPORT:
				ipts_dbg(ipts, "output hid feature report\n");
				break;
			case OUTPUT_BUFFER_PAYLOAD_KERNEL_LOAD:
				ipts_dbg(ipts, "output kernel load\n");
				break;
			case OUTPUT_BUFFER_PAYLOAD_FEEDBACK_BUFFER:
			{
				/* send feedback data for raw data mode */
                                fb_buf = ipts_get_feedback_buffer(ipts,
								parallel_idx);
				transaction_id = out_buf_hdr->
						hid_private_data.transaction_id;
				memcpy(fb_buf->addr, payload, payload_size);
				break;
			}
			case OUTPUT_BUFFER_PAYLOAD_ERROR:
			{
				kernel_output_payload_error_t *err_payload;

				if (payload_size == 0)
					break;

				err_payload =
					(kernel_output_payload_error_t*)payload;

				ipts_err(ipts, "error : severity : %d,"
						" source : %d,"
						" code : %d:%d:%d:%d\n"
						"string %s\n",
						err_payload->severity,
						err_payload->source,
						err_payload->code[0],
						err_payload->code[1],
						err_payload->code[2],
						err_payload->code[3],
						err_payload->string);
				
				break;
			}
			default:
				ipts_err(ipts, "invalid output buffer payload\n");
				break;
		}
	}

	if (fb_buf) {
		ret = ipts_send_feedback(ipts, parallel_idx, transaction_id);
		if (ret)
			return ret;
	}

	return 0;
}

static int handle_output_buffers(ipts_info_t *ipts, int cur_idx, int end_idx)
{
	int max_num_of_buffers = ipts_get_num_of_parallel_buffers(ipts);

	do {
		cur_idx++; /* cur_idx has last completed so starts with +1 */
		cur_idx %= max_num_of_buffers;
		handle_outputs(ipts, cur_idx);
	} while (cur_idx != end_idx);

	return 0;
}

int ipts_handle_processed_data(ipts_info_t *ipts)
{
	int ret = 0;
	int current_buffer_idx;
	int last_buffer_idx;

	current_buffer_idx = *ipts->last_submitted_id;
	last_buffer_idx = ipts->last_buffer_completed;

	if (current_buffer_idx == last_buffer_idx)
		return 0;

	ipts->last_buffer_completed = current_buffer_idx;
	handle_output_buffers(ipts, last_buffer_idx, current_buffer_idx);

	return ret;
}
