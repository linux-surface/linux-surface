/*
 * MEI client driver for Intel Precise Touch and Stylus
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

#include <linux/mei_cl_bus.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/hid.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/intel_ipts_if.h>

#include "ipts.h"
#include "ipts-hid.h"
#include "ipts-msg-handler.h"
#include "ipts-mei-msgs.h"
#include "ipts-binary-spec.h"
#include "ipts-state.h"

#define IPTS_DRIVER_NAME	"ipts"
#define IPTS_MEI_UUID		UUID_LE(0x3e8d0870, 0x271a, 0x4208, \
				0x8e, 0xb5, 0x9a, 0xcb, 0x94, 0x02, 0xae, 0x04)

static struct mei_cl_device_id ipts_mei_cl_tbl[] = {
	{ "", IPTS_MEI_UUID, MEI_CL_VERSION_ANY},
	{}
};

static ssize_t sensor_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ipts_info_t *ipts;
	ipts = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", ipts->sensor_mode);
}

//TODO: Verify the function implementation
static ssize_t sensor_mode_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int ret;
	long val;
	ipts_info_t *ipts;

	ipts = dev_get_drvdata(dev);
	ret = kstrtol(buf, 10, &val);
	if (ret)
	   return ret;

	ipts_dbg(ipts, "try sensor mode = %ld\n", val);

	switch (val) {
		case TOUCH_SENSOR_MODE_HID:
			break;
		case TOUCH_SENSOR_MODE_RAW_DATA:
			break;
		default:
			ipts_err(ipts, "sensor mode %ld is not supported\n", val);
	}

	return count;
}

static ssize_t device_info_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ipts_info_t *ipts;

	ipts = dev_get_drvdata(dev);
	return sprintf(buf, "vendor id = 0x%04hX\n"
				"device id = 0x%04hX\n"
				"HW rev = 0x%08X\n"
				"firmware rev = 0x%08X\n",
			ipts->device_info.vendor_id, ipts->device_info.device_id,
			ipts->device_info.hw_rev, ipts->device_info.fw_rev);
}

static DEVICE_ATTR_RW(sensor_mode);
static DEVICE_ATTR_RO(device_info);

static struct attribute *ipts_attrs[] = {
	&dev_attr_sensor_mode.attr,
	&dev_attr_device_info.attr,
	NULL
};

static const struct attribute_group ipts_grp = {
	.attrs = ipts_attrs,
};

MODULE_DEVICE_TABLE(mei, ipts_mei_cl_tbl);

static void raw_data_work_func(struct work_struct *work)
{
	ipts_info_t *ipts = container_of(work, ipts_info_t, raw_data_work);

	ipts_handle_processed_data(ipts);
}

static void gfx_status_work_func(struct work_struct *work)
{
	ipts_info_t *ipts = container_of(work, ipts_info_t, gfx_status_work);
	ipts_state_t state;
	int status = ipts->gfx_status;

	ipts_dbg(ipts, "notify gfx status : %d\n", status);

	state = ipts_get_state(ipts);

	if (state == IPTS_STA_RAW_DATA_STARTED || state == IPTS_STA_HID_STARTED) {
		if (status == IPTS_NOTIFY_STA_BACKLIGHT_ON &&
					ipts->display_status == false) {
			ipts_send_sensor_clear_mem_window_cmd(ipts);
			ipts->display_status = true;
		} else if (status == IPTS_NOTIFY_STA_BACKLIGHT_OFF &&
					ipts->display_status == true) {
			ipts_send_sensor_quiesce_io_cmd(ipts);
			ipts->display_status = false;
		}
	}
}

/* event loop */
static int ipts_mei_cl_event_thread(void *data)
{
	ipts_info_t *ipts = (ipts_info_t *)data;
	struct mei_cl_device *cldev = ipts->cldev;
	ssize_t msg_len;
	touch_sensor_msg_m2h_t m2h_msg;

	while (!kthread_should_stop()) {
		msg_len = mei_cldev_recv(cldev, (u8*)&m2h_msg, sizeof(m2h_msg));
		if (msg_len <= 0) {
			ipts_err(ipts, "error in reading m2h msg\n");
			continue;
		}

		if (ipts_handle_resp(ipts, &m2h_msg, msg_len) != 0) {
			ipts_err(ipts, "error in handling resp msg\n");
		}
	}

	ipts_dbg(ipts, "!! end event loop !!\n");

	return 0;
}

static void init_work_func(struct work_struct *work)
{
	ipts_info_t *ipts = container_of(work, ipts_info_t, init_work);

	ipts->sensor_mode = TOUCH_SENSOR_MODE_RAW_DATA;
	ipts->display_status = true;

	ipts_start(ipts);
}

static int ipts_mei_cl_probe(struct mei_cl_device *cldev,
			const struct mei_cl_device_id *id)
{
	int ret = 0;
	ipts_info_t *ipts = NULL;

	pr_info("probing Intel Precise Touch & Stylus\n");

	// setup the DMA BIT mask, the system will choose the best possible
	if (dma_coerce_mask_and_coherent(&cldev->dev, DMA_BIT_MASK(64)) == 0) {
		pr_info("IPTS using DMA_BIT_MASK(64)\n");
	} else if (dma_coerce_mask_and_coherent(&cldev->dev,
						DMA_BIT_MASK(32)) == 0) {
		pr_info("IPTS using  DMA_BIT_MASK(32)\n");
	} else {
		pr_err("IPTS: No suitable DMA available\n");
		return -EFAULT;
	}

	ret = mei_cldev_enable(cldev);
	if (ret < 0) {
		pr_err("cannot enable IPTS\n");
		return ret;
	}

	ipts = devm_kzalloc(&cldev->dev, sizeof(ipts_info_t), GFP_KERNEL);
	if (ipts == NULL) {
		ret = -ENOMEM;
		goto disable_mei;
	}
	ipts->cldev = cldev;
	mei_cldev_set_drvdata(cldev, ipts);

	ipts->event_loop = kthread_run(ipts_mei_cl_event_thread, (void*)ipts,
							"ipts_event_thread");

	if(ipts_dbgfs_register(ipts, "ipts"))
		pr_debug("cannot register debugfs for IPTS\n");

	INIT_WORK(&ipts->init_work, init_work_func);
	INIT_WORK(&ipts->raw_data_work, raw_data_work_func);
	INIT_WORK(&ipts->gfx_status_work, gfx_status_work_func);

	ret = sysfs_create_group(&cldev->dev.kobj, &ipts_grp);
	if (ret != 0) {
		pr_debug("cannot create sysfs for IPTS\n");
	}

	schedule_work(&ipts->init_work);

	return 0;

disable_mei :
	mei_cldev_disable(cldev);
	
	return ret;
}

static int ipts_mei_cl_remove(struct mei_cl_device *cldev)
{
	ipts_info_t *ipts = mei_cldev_get_drvdata(cldev);

	ipts_stop(ipts);

	sysfs_remove_group(&cldev->dev.kobj, &ipts_grp);
	ipts_hid_release(ipts);
	ipts_dbgfs_deregister(ipts);
	mei_cldev_disable(cldev);

	kthread_stop(ipts->event_loop);

	pr_info("IPTS removed\n");

	return 0;
}

static struct mei_cl_driver ipts_mei_cl_driver = {
	.id_table = ipts_mei_cl_tbl,
	.name = IPTS_DRIVER_NAME,
	.probe = ipts_mei_cl_probe,
	.remove = ipts_mei_cl_remove,
};

static int ipts_mei_cl_init(void)
{
	int ret;

	pr_info("IPTS %s() is called\n", __func__);

	ret = mei_cldev_driver_register(&ipts_mei_cl_driver);
	if (ret) {
		pr_err("unable to register IPTS mei client driver\n");
		return ret;
	}

	return 0;
}

static void __exit ipts_mei_cl_exit(void)
{
	pr_info("IPTS %s() is called\n", __func__);

	mei_cldev_driver_unregister(&ipts_mei_cl_driver);
}

module_init(ipts_mei_cl_init);
module_exit(ipts_mei_cl_exit);

MODULE_DESCRIPTION
	("Intel(R) Management Engine Interface Client Driver for "\
	"Intel Precision Touch and Sylus");
MODULE_LICENSE("GPL");
