/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#ifndef ATOMISP_PLATFORM_CSS15_H_
#define ATOMISP_PLATFORM_CSS15_H_

#include <linux/i2c.h>
#include <linux/sfi.h>
#include <media/v4l2-subdev.h>
#include "atomisp.h"

#define MAX_SENSORS_PER_PORT 4

enum atomisp_bayer_order {
	atomisp_bayer_order_grbg,
	atomisp_bayer_order_rggb,
	atomisp_bayer_order_bggr,
	atomisp_bayer_order_gbrg
};

enum atomisp_input_stream_id {
	ATOMISP_INPUT_STREAM_GENERAL = 0,
	ATOMISP_INPUT_STREAM_CAPTURE = 0,
	ATOMISP_INPUT_STREAM_POSTVIEW,
	ATOMISP_INPUT_STREAM_PREVIEW,
	ATOMISP_INPUT_STREAM_VIDEO,
	ATOMISP_INPUT_STREAM_NUM
};

enum atomisp_input_format {
	ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY,/* 8 bits per subpixel (legacy) */
	ATOMISP_INPUT_FORMAT_YUV420_8, /* 8 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV420_10,/* 10 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV422_8, /* UYVY..UVYV, 8 bits per subpixel */
	ATOMISP_INPUT_FORMAT_YUV422_10,/* UYVY..UVYV, 10 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_444,  /* BGR..BGR, 4 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_555,  /* BGR..BGR, 5 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_565,  /* BGR..BGR, 5 bits B and $, 6 bits G */
	ATOMISP_INPUT_FORMAT_RGB_666,  /* BGR..BGR, 6 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RGB_888,  /* BGR..BGR, 8 bits per subpixel */
	ATOMISP_INPUT_FORMAT_RAW_6,    /* RAW data, 6 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_7,    /* RAW data, 7 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_8,    /* RAW data, 8 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_10,   /* RAW data, 10 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_12,   /* RAW data, 12 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_14,   /* RAW data, 14 bits per pixel */
	ATOMISP_INPUT_FORMAT_RAW_16,   /* RAW data, 16 bits per pixel */
	ATOMISP_INPUT_FORMAT_BINARY_8, /* Binary byte stream. */
};

enum intel_v4l2_subdev_type {
	RAW_CAMERA = 1,
	SOC_CAMERA = 2,
	CAMERA_MOTOR = 3,
	LED_FLASH = 4,
	XENON_FLASH = 5,
	FILE_INPUT = 6,
	TEST_PATTERN = 7,
};

struct intel_v4l2_subdev_id {
	char name[17];
	enum intel_v4l2_subdev_type type;
	enum atomisp_camera_port    port;
};

struct intel_v4l2_subdev_i2c_board_info {
	struct i2c_board_info board_info;
	int i2c_adapter_id;
};

struct intel_v4l2_subdev_table {
	struct intel_v4l2_subdev_i2c_board_info v4l2_subdev;
	enum intel_v4l2_subdev_type type;
	enum atomisp_camera_port port;
};

struct atomisp_platform_data {
	struct intel_v4l2_subdev_table *subdevs;
	const struct soft_platform_id *spid;
};

/* Describe the capacities of one single sensor. */
struct atomisp_sensor_caps {
	/* The number of streams this sensor can output. */
	int stream_num;
};

/* Describe the capacities of sensors connected to one camera port. */
struct atomisp_camera_caps {
	/* The number of sensors connected to this camera port. */
	int sensor_num;
	/* The capacities of each sensor. */
	struct atomisp_sensor_caps sensor[MAX_SENSORS_PER_PORT];
};

struct atomisp_input_stream_info {
	enum atomisp_input_stream_id stream;
	unsigned int enable;
	/* Sensor driver fills ch_id with the id
	   of the virtual channel. */
	unsigned int ch_id;
};

struct camera_sensor_platform_data {
	int (*gpio_ctrl)(struct v4l2_subdev *subdev, int flag);
	int (*flisclk_ctrl)(struct v4l2_subdev *subdev, int flag);
	int (*power_ctrl)(struct v4l2_subdev *subdev, int flag);
	int (*csi_cfg)(struct v4l2_subdev *subdev, int flag);
	bool (*low_fps)(void);
	int (*platform_init)(struct i2c_client *);
	int (*platform_deinit)(void);
	char *(*msr_file_name)(void);
	struct atomisp_camera_caps *(*get_camera_caps)(void);
};

struct camera_af_platform_data {
	int (*power_ctrl)(struct v4l2_subdev *subdev, int flag);
};

const struct camera_af_platform_data *camera_get_af_platform_data(void);

struct camera_mipi_info {
	enum atomisp_camera_port        port;
	unsigned int                    num_lanes;
	enum atomisp_input_format       input_format;
	enum atomisp_bayer_order        raw_bayer_order;
	struct atomisp_sensor_mode_data data;
};

extern const struct atomisp_platform_data *atomisp_get_platform_data(void);
extern const struct atomisp_camera_caps *atomisp_get_default_camera_caps(void);

#endif /* ATOMISP_PLATFORM_H_ */
