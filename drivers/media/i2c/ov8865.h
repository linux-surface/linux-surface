/*
 * Support for Omnivision OV8865 camera sensor.
 * Based on Aptina mt9e013 driver.
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

#ifndef __OV8865_H__
#define __OV8865_H__
#include <linux/atomisp_platform.h>
#include <linux/atomisp.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <linux/types.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define to_bu64243_device(_sd) (&(container_of(_sd, struct ov8865_device, sd) \
				 ->bu64243))

#define BU64243_W0_W2(a) ((a & 0x07) << 3)
#define BU64243_PS(a) (a << 7)
#define BU64243_EN(a) (a << 6)
#define BU64243_M(a) (a << 2)
#define BU64243_D_HI(a) ((a >> 8) & 0x03)
#define BU64243_D_LO(a) (a & 0xff)
#define BU64243_RFEQ(a) ((a & 0x1f) << 3)
#define BU64243_SRATE(a) (a & 0x03)
#define BU64243_STIME(a) (a & 0x1f)
#define BU64243_SRES(a) ((a & 0x07) << 5)
#define BU64243_I2C_ADDR				0x0C
#define BU64243_VCM_CURRENT			0	/* point C */
#define BU64243_PARAM_1				1	/* freq and slew rate */
#define BU64243_PARAM_2				2	/* point A */
#define BU64243_PARAM_3				3   /* point B */
#define BU64243_PARAM_4				4	/*step res and step time*/



#define BU64243_DEFAULT_POINT_A				0x2a	/* 42	*/
#define BU64243_DEFAULT_POINT_B				0x52	/* 82 */
#define BU64243_DEFAULT_POINT_C				0xCB	/* 203*/
#define BU64243_DEFAULT_VCM_FREQ			0x08	/* 85HZ */
#define BU64243_DEFAULT_SLEW_RATE			0x03	/* fastest */
#define BU64243_DEFAULT_RES_SETTING			0x04	/* 4LSB */
#define BU64243_DEFAULT_STEP_TIME_SETTING	0x04	/* 200us */
#define BU64243_DEFAULT_OUTPUT_STATUS		0x01	/* 0 um	*/
#define BU64243_DEFAULT_ISRC_MODE			0x01	/* 0 um	*/

#define INTEL_FOCUS_OFFSET 20
#define VCM_ORIENTATION_OFFSET 100
#define POINT_AB_OFFSET 40

#define BU64243_MAX_FOCUS_POS 1023

/* bu64243 device structure */
struct bu64243_device {
	bool initialized;		/* true if bu64243 is detected */
	u16 focus;
	u16 power_state;
	u16 output_status;	/* */
	u16 isrc_mode;		/* ISRC enable */
	u16 point_a;			/* 0 um	*/
	u16 point_b;			/*	lens float */
	u16 point_c;			/* focus point*/
	u16 res_freq;		/* resonant frequency*/
	u16 slew_rate;		/* slew rate*/
	u16 step_res;		/* step resolution */
	u16 step_time;		/* step time */
	struct timespec focus_time;	/* Time when focus was last time set */
	__u8 buffer[4];			/* Used for i2c transactions */
	const struct camera_af_platform_data *platform_data;
};

#define	OV8865_NAME	"ov8865"
#define	OV8865_ADDR	0x10


#define OV8865_CHIP_ID	0x8865


#define	LAST_REG_SETING		{0xffff, 0xff}
#define	is_last_reg_setting(item) ((item).reg == 0xffff)
#define I2C_MSG_LENGTH		0x2

#define OV8865_INVALID_CONFIG	0xffffffff

#define OV8865_INTG_UNIT_US	100
#define OV8865_MCLK		192

#define OV8865_REG_BITS	16
#define OV8865_REG_MASK	0xFFFF

/* This should be added into include/linux/videodev2.h */
#ifndef V4L2_IDENT_OV8865
#define V4L2_IDENT_OV8865	8245
#endif

/*
 * ov8865 System control registers
 */
#define OV8865_PLL_PLL10			0x3090
#define OV8865_PLL_PLL11			0x3091
#define OV8865_PLL_PLL12			0x3092
#define OV8865_PLL_PLL13			0x3093
#define OV8865_TIMING_VTS			0x380e
#define OV8865_TIMING_HTS			0x380C

#define PLL1_SYS_PRE_DIV			0x030F

#define OV8865_HORIZONTAL_START_H		0x3800
#define OV8865_HORIZONTAL_START_L		0x3801
#define OV8865_VERTICAL_START_H			0x3802
#define OV8865_VERTICAL_START_L			0x3803
#define OV8865_HORIZONTAL_END_H			0x3804
#define OV8865_HORIZONTAL_END_L			0x3805
#define OV8865_VERTICAL_END_H			0x3806
#define OV8865_VERTICAL_END_L			0x3807
#define OV8865_HORIZONTAL_OUTPUT_SIZE_H		0x3808
#define OV8865_HORIZONTAL_OUTPUT_SIZE_L		0x3809
#define OV8865_VERTICAL_OUTPUT_SIZE_H		0x380a
#define OV8865_VERTICAL_OUTPUT_SIZE_L		0x380b

#define OV8865_GROUP_ACCESS			0x3208
#define OV8865_GROUP_ACCESS_HOLD_START		0x00
#define OV8865_GROUP_ACCESS_HOLD_END		0x10
#define OV8865_GROUP_ACCESS_DELAY_LAUNCH	0xA0
#define OV8865_GROUP_ACCESS_QUICK_LAUNCH	0xE0

#define OV8865_LONG_EXPO			0x3500
#define OV8865_AGC_ADJ				0x3508
#define OV8865_TEST_PATTERN_MODE		0x3070

/* ov8865 SCCB */
#define OV8865_SCCB_CTRL			0x3100
#define OV8865_AEC_PK_EXPO_H			0x3500
#define OV8865_AEC_PK_EXPO_M			0x3501
#define OV8865_AEC_PK_EXPO_L			0x3502
#define OV8865_AEC_MANUAL_CTRL			0x3503
#define OV8865_AGC_ADJ_H			0x3508
#define OV8865_AGC_ADJ_L			0x3509

#define OV8865_MWB_RED_GAIN_H			0x3400
#define OV8865_MWB_GREEN_GAIN_H			0x3402
#define OV8865_MWB_BLUE_GAIN_H			0x3404
#define OV8865_DIGI_GAIN				0x350A
#define OV8865_MWB_GAIN_MAX			0x3fff

#define OV8865_OTP_BANK0_PID			0x3d00
#define OV8865_CHIP_ID_HIGH			0x300B
#define OV8865_CHIP_ID_LOW			0x300C
#define OV8865_STREAM_MODE			0x0100

#define OV8865_FOCAL_LENGTH_NUM	439	/*4.39mm*/
#define OV8865_FOCAL_LENGTH_DEM	100
#define OV8865_F_NUMBER_DEFAULT_NUM	22
#define OV8865_F_NUMBER_DEM	10

#define OV8865_TIMING_X_INC		0x3814
#define OV8865_TIMING_Y_INC		0x382A

#define OV8865_ISP_CTRL 0x501E

/* sensor_mode_data read_mode adaptation */
#define OV8865_READ_MODE_BINNING_ON	0x0400
#define OV8865_READ_MODE_BINNING_OFF	0x00
#define OV8865_INTEGRATION_TIME_MARGIN	4

#define OV8865_MAX_VTS_VALUE		0x7FFF
#define OV8865_MAX_EXPOSURE_VALUE \
		(OV8865_MAX_VTS_VALUE - OV8865_INTEGRATION_TIME_MARGIN)
#define OV8865_MAX_GAIN_VALUE		0xfff /*x32 gain*/

/*
 * focal length bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define OV8865_FOCAL_LENGTH_DEFAULT ((385<<16)|100) //3.85

/*
 * current f-number bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define OV8865_F_NUMBER_DEFAULT ((220<<16)|100) //2.2

/*
 * f-number range bits definition:
 * bits 31-24: max f-number numerator
 * bits 23-16: max f-number denominator
 * bits 15-8: min f-number numerator
 * bits 7-0: min f-number denominator
 */
#define OV8865_F_NUMBER_RANGE 0x180a180a
#define OTPM_ADD_START_1		0x1000
#define OTPM_DATA_LENGTH_1		0x0100
#define OTPM_COUNT 0x200

/* Defines for register writes and register array processing */
#define OV8865_BYTE_MAX	32
#define OV8865_SHORT_MAX	16
#define I2C_RETRY_COUNT		5
#define OV8865_TOK_MASK	0xfff0

#define	OV8865_STATUS_POWER_DOWN	0x0
#define	OV8865_STATUS_STANDBY		0x2
#define	OV8865_STATUS_ACTIVE		0x3
#define	OV8865_STATUS_VIEWFINDER	0x4

#define MAX_FPS_OPTIONS_SUPPORTED	3

#define	v4l2_format_capture_type_entry(_width, _height, \
		_pixelformat, _bytesperline, _colorspace) \
	{\
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,\
		.fmt.pix.width = (_width),\
		.fmt.pix.height = (_height),\
		.fmt.pix.pixelformat = (_pixelformat),\
		.fmt.pix.bytesperline = (_bytesperline),\
		.fmt.pix.colorspace = (_colorspace),\
		.fmt.pix.sizeimage = (_height)*(_bytesperline),\
	}

#define	s_output_format_entry(_width, _height, _pixelformat, \
		_bytesperline, _colorspace, _fps) \
	{\
		.v4l2_fmt = v4l2_format_capture_type_entry(_width, \
			_height, _pixelformat, _bytesperline, \
				_colorspace),\
		.fps = (_fps),\
	}

#define	s_output_format_reg_entry(_width, _height, _pixelformat, \
		_bytesperline, _colorspace, _fps, _reg_setting) \
	{\
		.s_fmt = s_output_format_entry(_width, _height,\
				_pixelformat, _bytesperline, \
				_colorspace, _fps),\
		.reg_setting = (_reg_setting),\
	}

struct s_ctrl_id {
	struct v4l2_queryctrl qc;
	int (*s_ctrl)(struct v4l2_subdev *sd, u32 val);
	int (*g_ctrl)(struct v4l2_subdev *sd, u32 *val);
};

#define	v4l2_queryctrl_entry_integer(_id, _name,\
		_minimum, _maximum, _step, \
		_default_value, _flags)	\
	{\
		.id = (_id), \
		.type = V4L2_CTRL_TYPE_INTEGER, \
		.name = _name, \
		.minimum = (_minimum), \
		.maximum = (_maximum), \
		.step = (_step), \
		.default_value = (_default_value),\
		.flags = (_flags),\
	}
#define	v4l2_queryctrl_entry_boolean(_id, _name,\
		_default_value, _flags)	\
	{\
		.id = (_id), \
		.type = V4L2_CTRL_TYPE_BOOLEAN, \
		.name = _name, \
		.minimum = 0, \
		.maximum = 1, \
		.step = 1, \
		.default_value = (_default_value),\
		.flags = (_flags),\
	}

#define	s_ctrl_id_entry_integer(_id, _name, \
		_minimum, _maximum, _step, \
		_default_value, _flags, \
		_s_ctrl, _g_ctrl)	\
	{\
		.qc = v4l2_queryctrl_entry_integer(_id, _name,\
				_minimum, _maximum, _step,\
				_default_value, _flags), \
		.s_ctrl = _s_ctrl, \
		.g_ctrl = _g_ctrl, \
	}

#define	s_ctrl_id_entry_boolean(_id, _name, \
		_default_value, _flags, \
		_s_ctrl, _g_ctrl)	\
	{\
		.qc = v4l2_queryctrl_entry_boolean(_id, _name,\
				_default_value, _flags), \
		.s_ctrl = _s_ctrl, \
		.g_ctrl = _g_ctrl, \
	}


#define	macro_string_entry(VAL)	\
	{ \
		.val = VAL, \
		.string = #VAL, \
	}

enum ov8865_tok_type {
	OV8865_8BIT  = 0x0001,
	OV8865_16BIT = 0x0002,
	OV8865_TOK_TERM   = 0xf000,	/* terminating token for reg list */
	OV8865_TOK_DELAY  = 0xfe00	/* delay token for reg list */
};

/*
 * If register address or register width is not 32 bit width,
 * user needs to convert it manually
 */

struct s_register_setting {
	u32 reg;
	u32 val;
};

struct s_output_format {
	struct v4l2_format v4l2_fmt;
	int fps;
};

/**
 * struct ov8865_fwreg - Firmware burst command
 * @type: FW burst or 8/16 bit register
 * @addr: 16-bit offset to register or other values depending on type
 * @val: data value for burst (or other commands)
 *
 * Define a structure for sensor register initialization values
 */
struct ov8865_fwreg {
	enum ov8865_tok_type type; /* value, register or FW burst string */
	u16 addr;	/* target address */
	u32 val[8];
};

/**
 * struct ov8865_reg - MI sensor  register format
 * @type: type of the register
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 *
 * Define a structure for sensor register initialization values
 */
struct ov8865_reg {
	enum ov8865_tok_type type;
	union {
		u16 sreg;
		struct ov8865_fwreg *fwreg;
	} reg;
	u32 val;	/* @set value for read/mod/write, @mask */
	u32 val2;	/* optional: for rmw, OR mask */
};

struct ov8865_fps_setting {
	int fps;
	unsigned short pixels_per_line;
	unsigned short lines_per_frame;
};

/* Store macro values' debug names */
struct macro_string {
	u8 val;
	char *string;
};

static inline const char *
macro_to_string(const struct macro_string *array, int size, u8 val)
{
	int i;
	for (i = 0; i < size; i++) {
		if (array[i].val == val)
			return array[i].string;
	}
	return "Unknown VAL";
}

struct ov8865_control {
	struct v4l2_queryctrl qc;
	int (*query)(struct v4l2_subdev *sd, s32 *value);
	int (*tweak)(struct v4l2_subdev *sd, s32 value);
};

struct ov8865_resolution {
	u8 *desc;
	int res;
	int width;
	int height;
	bool used;
	const struct ov8865_reg *regs;
	u8 bin_factor_x;
	u8 bin_factor_y;
	unsigned short skip_frames;
	const struct ov8865_fps_setting fps_options[MAX_FPS_OPTIONS_SUPPORTED];
};

struct ov8865_format {
	u8 *desc;
	u32 pixelformat;
	struct s_register_setting *regs;
};

/* ov8865 device structure */
struct ov8865_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;

	u8 *otp_data;
	struct camera_sensor_platform_data *platform_data;
	int fmt_idx;
	int streaming;
	int power;
	u16 sensor_id;
	u8 sensor_revision;
	int exposure;
	int gain;
	u16 digital_gain;
	struct bu64243_device bu64243;
	struct mutex input_lock; /* serialize sensor's ioctl */

	const struct ov8865_reg *basic_settings_list;
	const struct ov8865_resolution *curr_res_table;
	int entries_curr_table;
	int fps_index;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *run_mode;
};

/*
 * The i2c adapter on Intel Medfield can transfer 32 bytes maximum
 * at a time. In burst mode we require that the buffer is transferred
 * in one shot, so limit the buffer size to 32 bytes minus a safety.
 */
#define OV8865_MAX_WRITE_BUF_SIZE	30
struct ov8865_write_buffer {
	u16 addr;
	u8 data[OV8865_MAX_WRITE_BUF_SIZE];
};

struct ov8865_write_ctrl {
	int index;
	struct ov8865_write_buffer buffer;
};

#define OV8865_RES_WIDTH_MAX	3280
#define OV8865_RES_HEIGHT_MAX	2464

/* please refer to 0x3800 0x3801 setting*/
#define OV8865_CROP_X_START_FOR_MAX_SIZE 0x0C
/* please refer to 0x3802 0x3803 setting*/
#define OV8865_CROP_Y_START_FOR_MAX_SIZE 0x0C

#define ISP_PADDING_W 16
#define ISP_PADDING_H 16
#define OV8865_ISP_MAX_WIDTH	(OV8865_RES_WIDTH_MAX - ISP_PADDING_W)
#define OV8865_ISP_MAX_HEIGHT	(OV8865_RES_HEIGHT_MAX - ISP_PADDING_H)

static const struct ov8865_reg ov8865_BasicSettings[] = {
	{OV8865_8BIT, {0x0103}, 0x01},
	/* delay 10ms*/
	{OV8865_TOK_DELAY, {0}, 0x0A},

	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x3638}, 0xff},
	{OV8865_8BIT, {0x0300}, 0x05},
	{OV8865_8BIT, {0x0302}, 0x96},
	{OV8865_8BIT, {0x0303}, 0x00},
	{OV8865_8BIT, {0x0304}, 0x03},
	{OV8865_8BIT, {0x030b}, 0x05},
	{OV8865_8BIT, {0x030e}, 0x00},
	{OV8865_8BIT, {0x030d}, 0x96},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x0312}, 0x01},
	{OV8865_8BIT, {0x031e}, 0x0c},
	{OV8865_8BIT, {0x3015}, 0x01},
	{OV8865_8BIT, {0x3018}, 0x72},
	{OV8865_8BIT, {0x3020}, 0x93},
	{OV8865_8BIT, {0x3022}, 0x01},
	{OV8865_8BIT, {0x3031}, 0x0a},
	{OV8865_8BIT, {0x3106}, 0x01},
	{OV8865_8BIT, {0x3305}, 0xf1},
	{OV8865_8BIT, {0x3308}, 0x00},
	{OV8865_8BIT, {0x3309}, 0x28},
	{OV8865_8BIT, {0x330a}, 0x00},
	{OV8865_8BIT, {0x330b}, 0x20},
	{OV8865_8BIT, {0x330c}, 0x00},
	{OV8865_8BIT, {0x330d}, 0x00},
	{OV8865_8BIT, {0x330e}, 0x00},
	{OV8865_8BIT, {0x330f}, 0x40},
	{OV8865_8BIT, {0x3307}, 0x04},
	{OV8865_8BIT, {0x3604}, 0x04},
	{OV8865_8BIT, {0x3602}, 0x30},
	{OV8865_8BIT, {0x3605}, 0x00},
	{OV8865_8BIT, {0x3607}, 0x20},
	{OV8865_8BIT, {0x3608}, 0x11},
	{OV8865_8BIT, {0x3609}, 0x68},
	{OV8865_8BIT, {0x360a}, 0x40},
	{OV8865_8BIT, {0x360c}, 0xdd},
	{OV8865_8BIT, {0x360e}, 0x0c},
	{OV8865_8BIT, {0x3610}, 0x07},
	{OV8865_8BIT, {0x3612}, 0x86},
	{OV8865_8BIT, {0x3613}, 0x58},
	{OV8865_8BIT, {0x3614}, 0x28},
	{OV8865_8BIT, {0x3617}, 0x40},
	{OV8865_8BIT, {0x3618}, 0x5a},
	{OV8865_8BIT, {0x3619}, 0x9b},
	{OV8865_8BIT, {0x361c}, 0x00},
	{OV8865_8BIT, {0x361d}, 0x60},
	{OV8865_8BIT, {0x3631}, 0x60},
	{OV8865_8BIT, {0x3633}, 0x10},
	{OV8865_8BIT, {0x3634}, 0x10},
	{OV8865_8BIT, {0x3635}, 0x10},
	{OV8865_8BIT, {0x3636}, 0x10},
	{OV8865_8BIT, {0x3641}, 0x55},
	{OV8865_8BIT, {0x3646}, 0x86},
	{OV8865_8BIT, {0x3647}, 0x27},
	{OV8865_8BIT, {0x364a}, 0x1b},
	{OV8865_8BIT, {0x3500}, 0x00},
	{OV8865_8BIT, {0x3503}, 0x00},
	{OV8865_8BIT, {0x3509}, 0x00},
	{OV8865_8BIT, {0x3705}, 0x00},
	{OV8865_8BIT, {0x3719}, 0x31},
	{OV8865_8BIT, {0x3714}, 0x12},
	{OV8865_8BIT, {0x3733}, 0x10},
	{OV8865_8BIT, {0x3734}, 0x40},
	{OV8865_8BIT, {0x3755}, 0x40},
	{OV8865_8BIT, {0x3758}, 0x00},
	{OV8865_8BIT, {0x3759}, 0x4c},
	{OV8865_8BIT, {0x375c}, 0x40},
	{OV8865_8BIT, {0x375e}, 0x00},
	{OV8865_8BIT, {0x3768}, 0x04},
	{OV8865_8BIT, {0x3769}, 0x20},
	{OV8865_8BIT, {0x376c}, 0xc0},
	{OV8865_8BIT, {0x376d}, 0xc0},
	{OV8865_8BIT, {0x376a}, 0x08},
	{OV8865_8BIT, {0x3761}, 0x00},
	{OV8865_8BIT, {0x3762}, 0x00},
	{OV8865_8BIT, {0x3763}, 0x00},
	{OV8865_8BIT, {0x3766}, 0xff},
	{OV8865_8BIT, {0x376b}, 0x42},
	{OV8865_8BIT, {0x37a4}, 0x00},
	{OV8865_8BIT, {0x37a6}, 0x00},
	{OV8865_8BIT, {0x3760}, 0x00},
	{OV8865_8BIT, {0x376f}, 0x01},
	{OV8865_8BIT, {0x37b0}, 0x00},
	{OV8865_8BIT, {0x37b1}, 0x00},
	{OV8865_8BIT, {0x37b2}, 0x00},
	{OV8865_8BIT, {0x37b6}, 0x00},
	{OV8865_8BIT, {0x37b7}, 0x00},
	{OV8865_8BIT, {0x37b8}, 0x00},
	{OV8865_8BIT, {0x37b9}, 0xff},
	{OV8865_8BIT, {0x3800}, 0x00},	/*please change OV8865_CROP_X_START_FOR_MAX_SIZE accordingly*/
	{OV8865_8BIT, {0x3801}, 0x0c},
	{OV8865_8BIT, {0x3802}, 0x00},	/*please change OV8865_CROP_Y_START_FOR_MAX_SIZE accordingly*/
	{OV8865_8BIT, {0x3803}, 0x0c},
	{OV8865_8BIT, {0x3804}, 0x0c},
	{OV8865_8BIT, {0x3805}, 0xd3},
	{OV8865_8BIT, {0x3806}, 0x09},
	{OV8865_8BIT, {0x3807}, 0xa3},
	{OV8865_8BIT, {0x3810}, 0x00},
	{OV8865_8BIT, {0x3811}, 0x04},
	{OV8865_8BIT, {0x3815}, 0x01},
	{OV8865_8BIT, {0x3820}, 0x06},
	{OV8865_8BIT, {0x382b}, 0x01},
	{OV8865_8BIT, {0x3837}, 0x18},
	{OV8865_8BIT, {0x3841}, 0xff},
	{OV8865_8BIT, {0x3d85}, 0x06},
	{OV8865_8BIT, {0x3d8c}, 0x75},
	{OV8865_8BIT, {0x3d8d}, 0xef},
	{OV8865_8BIT, {0x4000}, 0xf1},
	{OV8865_8BIT, {0x4005}, 0x10},
	{OV8865_8BIT, {0x400b}, 0x0c},
	{OV8865_8BIT, {0x400d}, 0x10},
	{OV8865_8BIT, {0x401b}, 0x00},
	{OV8865_8BIT, {0x401d}, 0x00},
	{OV8865_8BIT, {0x4028}, 0x00},
	{OV8865_8BIT, {0x4029}, 0x02},
	{OV8865_8BIT, {0x402a}, 0x04},
	{OV8865_8BIT, {0x402b}, 0x04},
	{OV8865_8BIT, {0x402c}, 0x02},
	{OV8865_8BIT, {0x402d}, 0x02},
	{OV8865_8BIT, {0x402e}, 0x08},
	{OV8865_8BIT, {0x402f}, 0x02},
	{OV8865_8BIT, {0x401f}, 0x00},
	{OV8865_8BIT, {0x4034}, 0x3f},
	{OV8865_8BIT, {0x4300}, 0xff},
	{OV8865_8BIT, {0x4301}, 0x00},
	{OV8865_8BIT, {0x4302}, 0x0f},
	{OV8865_8BIT, {0x4503}, 0x10},
	{OV8865_8BIT, {0x481f}, 0x32},
	{OV8865_8BIT, {0x4837}, 0x16},
	{OV8865_8BIT, {0x4850}, 0x10},
	{OV8865_8BIT, {0x4851}, 0x32},
	{OV8865_8BIT, {0x4b00}, 0x2a},
	{OV8865_8BIT, {0x4b0d}, 0x00},
	{OV8865_8BIT, {0x4d00}, 0x04},
	{OV8865_8BIT, {0x4d01}, 0x18},
	{OV8865_8BIT, {0x4d02}, 0xc3},
	{OV8865_8BIT, {0x4d03}, 0xff},
	{OV8865_8BIT, {0x4d04}, 0xff},
	{OV8865_8BIT, {0x4d05}, 0xff},
	{OV8865_8BIT, {0x5000}, 0x96},
	{OV8865_8BIT, {0x5001}, 0x01},
	{OV8865_8BIT, {0x5002}, 0x08},
	{OV8865_8BIT, {0x5901}, 0x00},
	{OV8865_8BIT, {0x5e00}, 0x00},
	{OV8865_8BIT, {0x5e01}, 0x41},
	{OV8865_8BIT, {0x5b00}, 0x02},
	{OV8865_8BIT, {0x5b01}, 0xd0},
	{OV8865_8BIT, {0x5b02}, 0x03},
	{OV8865_8BIT, {0x5b03}, 0xff},
	{OV8865_8BIT, {0x5b05}, 0x6c},
	{OV8865_8BIT, {0x5780}, 0xfc},
	{OV8865_8BIT, {0x5781}, 0xdf},
	{OV8865_8BIT, {0x5782}, 0x3f},
	{OV8865_8BIT, {0x5783}, 0x08},
	{OV8865_8BIT, {0x5784}, 0x0c},
	{OV8865_8BIT, {0x5786}, 0x20},
	{OV8865_8BIT, {0x5787}, 0x40},
	{OV8865_8BIT, {0x5788}, 0x08},
	{OV8865_8BIT, {0x5789}, 0x08},
	{OV8865_8BIT, {0x578a}, 0x02},
	{OV8865_8BIT, {0x578b}, 0x01},
	{OV8865_8BIT, {0x578c}, 0x01},
	{OV8865_8BIT, {0x578d}, 0x0c},
	{OV8865_8BIT, {0x578e}, 0x02},
	{OV8865_8BIT, {0x578f}, 0x01},
	{OV8865_8BIT, {0x5790}, 0x01},
	{OV8865_8BIT, {0x5800}, 0x1d},
	{OV8865_8BIT, {0x5801}, 0x0e},
	{OV8865_8BIT, {0x5802}, 0x0c},
	{OV8865_8BIT, {0x5803}, 0x0c},
	{OV8865_8BIT, {0x5804}, 0x0f},
	{OV8865_8BIT, {0x5805}, 0x22},
	{OV8865_8BIT, {0x5806}, 0x0a},
	{OV8865_8BIT, {0x5807}, 0x06},
	{OV8865_8BIT, {0x5808}, 0x05},
	{OV8865_8BIT, {0x5809}, 0x05},
	{OV8865_8BIT, {0x580a}, 0x07},
	{OV8865_8BIT, {0x580b}, 0x0a},
	{OV8865_8BIT, {0x580c}, 0x06},
	{OV8865_8BIT, {0x580d}, 0x02},
	{OV8865_8BIT, {0x580e}, 0x00},
	{OV8865_8BIT, {0x580f}, 0x00},
	{OV8865_8BIT, {0x5810}, 0x03},
	{OV8865_8BIT, {0x5811}, 0x07},
	{OV8865_8BIT, {0x5812}, 0x06},
	{OV8865_8BIT, {0x5813}, 0x02},
	{OV8865_8BIT, {0x5814}, 0x00},
	{OV8865_8BIT, {0x5815}, 0x00},
	{OV8865_8BIT, {0x5816}, 0x03},
	{OV8865_8BIT, {0x5817}, 0x07},
	{OV8865_8BIT, {0x5818}, 0x09},
	{OV8865_8BIT, {0x5819}, 0x06},
	{OV8865_8BIT, {0x581a}, 0x04},
	{OV8865_8BIT, {0x581b}, 0x04},
	{OV8865_8BIT, {0x581c}, 0x06},
	{OV8865_8BIT, {0x581d}, 0x0a},
	{OV8865_8BIT, {0x581e}, 0x19},
	{OV8865_8BIT, {0x581f}, 0x0d},
	{OV8865_8BIT, {0x5820}, 0x0b},
	{OV8865_8BIT, {0x5821}, 0x0b},
	{OV8865_8BIT, {0x5822}, 0x0e},
	{OV8865_8BIT, {0x5823}, 0x22},
	{OV8865_8BIT, {0x5824}, 0x23},
	{OV8865_8BIT, {0x5825}, 0x28},
	{OV8865_8BIT, {0x5826}, 0x29},
	{OV8865_8BIT, {0x5827}, 0x27},
	{OV8865_8BIT, {0x5828}, 0x13},
	{OV8865_8BIT, {0x5829}, 0x26},
	{OV8865_8BIT, {0x582a}, 0x33},
	{OV8865_8BIT, {0x582b}, 0x32},
	{OV8865_8BIT, {0x582c}, 0x33},
	{OV8865_8BIT, {0x582d}, 0x16},
	{OV8865_8BIT, {0x582e}, 0x14},
	{OV8865_8BIT, {0x582f}, 0x30},
	{OV8865_8BIT, {0x5830}, 0x31},
	{OV8865_8BIT, {0x5831}, 0x30},
	{OV8865_8BIT, {0x5832}, 0x15},
	{OV8865_8BIT, {0x5833}, 0x26},
	{OV8865_8BIT, {0x5834}, 0x23},
	{OV8865_8BIT, {0x5835}, 0x21},
	{OV8865_8BIT, {0x5836}, 0x23},
	{OV8865_8BIT, {0x5837}, 0x05},
	{OV8865_8BIT, {0x5838}, 0x36},
	{OV8865_8BIT, {0x5839}, 0x27},
	{OV8865_8BIT, {0x583a}, 0x28},
	{OV8865_8BIT, {0x583b}, 0x26},
	{OV8865_8BIT, {0x583c}, 0x24},
	{OV8865_8BIT, {0x583d}, 0xdf},

	/* otp key setting */
	{OV8865_8BIT, {0x3820}, 0x06},	/* Mirror off, flip on */
	{OV8865_8BIT, {0x3821}, 0x40},	/* Align with Intel/Lenovo requirement */
	{OV8865_8BIT, {0x5000}, 0x16},
	{OV8865_8BIT, {0x5018}, 0x10},
	{OV8865_8BIT, {0x5019}, 0x00},
	{OV8865_8BIT, {0x501a}, 0x10},
	{OV8865_8BIT, {0x501b}, 0x00},
	{OV8865_8BIT, {0x501c}, 0x10},
	{OV8865_8BIT, {0x501d}, 0x00},
	{OV8865_8BIT, {0x501e}, 0x00},

	{OV8865_8BIT, {0x4000}, 0xf3},
	{OV8865_8BIT, {0x3503}, 0x00},
	{OV8865_8BIT, {0x3501}, 0x07},
	{OV8865_8BIT, {0x3502}, 0xff},

	{OV8865_8BIT, {0x3508}, 0x00},

	{ OV8865_TOK_TERM, {0}, 0}
};

/*****************************STILL********************************/

static const struct ov8865_reg ov8865_896x736_30fps[] = {
/* 896x736_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x09},
	{OV8865_8BIT, {0x3501}, 0x31},
	{OV8865_8BIT, {0x3502}, 0x00},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x24},
	{OV8865_8BIT, {0x3701}, 0x0c},
	{OV8865_8BIT, {0x3702}, 0x28},
	{OV8865_8BIT, {0x3703}, 0x19},
	{OV8865_8BIT, {0x3704}, 0x14},
	{OV8865_8BIT, {0x3706}, 0x38},
	{OV8865_8BIT, {0x3707}, 0x04},
	{OV8865_8BIT, {0x3708}, 0x24},
	{OV8865_8BIT, {0x3709}, 0x40},
	{OV8865_8BIT, {0x370a}, 0x00},
	{OV8865_8BIT, {0x370b}, 0xb8},
	{OV8865_8BIT, {0x370c}, 0x04},
	{OV8865_8BIT, {0x3718}, 0x12},
	{OV8865_8BIT, {0x3712}, 0x42},
	{OV8865_8BIT, {0x371e}, 0x19},
	{OV8865_8BIT, {0x371f}, 0x40},
	{OV8865_8BIT, {0x3720}, 0x05},
	{OV8865_8BIT, {0x3721}, 0x05},
	{OV8865_8BIT, {0x3724}, 0x02},
	{OV8865_8BIT, {0x3725}, 0x02},
	{OV8865_8BIT, {0x3726}, 0x06},
	{OV8865_8BIT, {0x3728}, 0x05},
	{OV8865_8BIT, {0x3729}, 0x02},
	{OV8865_8BIT, {0x372a}, 0x03},
	{OV8865_8BIT, {0x372b}, 0x53},
	{OV8865_8BIT, {0x372c}, 0xa3},
	{OV8865_8BIT, {0x372d}, 0x53},
	{OV8865_8BIT, {0x372e}, 0x06},
	{OV8865_8BIT, {0x372f}, 0x10},
	{OV8865_8BIT, {0x3730}, 0x01},
	{OV8865_8BIT, {0x3731}, 0x06},
	{OV8865_8BIT, {0x3732}, 0x14},
	{OV8865_8BIT, {0x3736}, 0x20},
	{OV8865_8BIT, {0x373a}, 0x02},
	{OV8865_8BIT, {0x373b}, 0x0c},
	{OV8865_8BIT, {0x373c}, 0x0a},
	{OV8865_8BIT, {0x373e}, 0x03},
	{OV8865_8BIT, {0x375a}, 0x06},
	{OV8865_8BIT, {0x375b}, 0x13},
	{OV8865_8BIT, {0x375d}, 0x02},
	{OV8865_8BIT, {0x375f}, 0x14},
	{OV8865_8BIT, {0x3767}, 0x1c},
	{OV8865_8BIT, {0x3772}, 0x23},
	{OV8865_8BIT, {0x3773}, 0x02},
	{OV8865_8BIT, {0x3774}, 0x16},
	{OV8865_8BIT, {0x3775}, 0x12},
	{OV8865_8BIT, {0x3776}, 0x08},
	{OV8865_8BIT, {0x37a0}, 0x44},
	{OV8865_8BIT, {0x37a1}, 0x3d},
	{OV8865_8BIT, {0x37a2}, 0x3d},
	{OV8865_8BIT, {0x37a3}, 0x01},
	{OV8865_8BIT, {0x37a5}, 0x08},
	{OV8865_8BIT, {0x37a7}, 0x44},
	{OV8865_8BIT, {0x37a8}, 0x58},
	{OV8865_8BIT, {0x37a9}, 0x58},
	{OV8865_8BIT, {0x37aa}, 0x44},
	{OV8865_8BIT, {0x37ab}, 0x2e},
	{OV8865_8BIT, {0x37ac}, 0x2e},
	{OV8865_8BIT, {0x37ad}, 0x33},
	{OV8865_8BIT, {0x37ae}, 0x0d},
	{OV8865_8BIT, {0x37af}, 0x0d},
	{OV8865_8BIT, {0x37b3}, 0x42},
	{OV8865_8BIT, {0x37b4}, 0x42},
	{OV8865_8BIT, {0x37b5}, 0x33},
	{OV8865_8BIT, {0x3808}, 0x03},
	{OV8865_8BIT, {0x3809}, 0x80},
	{OV8865_8BIT, {0x380a}, 0x02},
	{OV8865_8BIT, {0x380b}, 0xe0},
	{OV8865_8BIT, {0x380c}, 0x06},
	{OV8865_8BIT, {0x380d}, 0xe6},
	{OV8865_8BIT, {0x380e}, 0x05},
	{OV8865_8BIT, {0x380f}, 0x50},
	{OV8865_8BIT, {0x3813}, 0x04},
	{OV8865_8BIT, {0x3814}, 0x03},
	{OV8865_8BIT, {0x3821}, 0x61},
	{OV8865_8BIT, {0x382a}, 0x03},
	{OV8865_8BIT, {0x3830}, 0x08},
	{OV8865_8BIT, {0x3836}, 0x02},
	{OV8865_8BIT, {0x3846}, 0x88},
	{OV8865_8BIT, {0x3f08}, 0x0b},
	{OV8865_8BIT, {0x4001}, 0x14},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x01},
	{OV8865_8BIT, {0x4023}, 0x56},
	{OV8865_8BIT, {0x4024}, 0x03},
	{OV8865_8BIT, {0x4025}, 0x54},
	{OV8865_8BIT, {0x4026}, 0x03},
	{OV8865_8BIT, {0x4027}, 0x84},
	{OV8865_8BIT, {0x4500}, 0x40},
	{OV8865_8BIT, {0x4601}, 0x38},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_1216x736_30fps[] = {
	/* 1216x736_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x09},
	{OV8865_8BIT, {0x3501}, 0x31},
	{OV8865_8BIT, {0x3502}, 0x00},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x24},
	{OV8865_8BIT, {0x3701}, 0x0c},
	{OV8865_8BIT, {0x3702}, 0x28},
	{OV8865_8BIT, {0x3703}, 0x19},
	{OV8865_8BIT, {0x3704}, 0x14},
	{OV8865_8BIT, {0x3706}, 0x38},
	{OV8865_8BIT, {0x3707}, 0x04},
	{OV8865_8BIT, {0x3708}, 0x24},
	{OV8865_8BIT, {0x3709}, 0x40},
	{OV8865_8BIT, {0x370a}, 0x00},
	{OV8865_8BIT, {0x370b}, 0xb8},
	{OV8865_8BIT, {0x370c}, 0x04},
	{OV8865_8BIT, {0x3718}, 0x12},
	{OV8865_8BIT, {0x3712}, 0x42},
	{OV8865_8BIT, {0x371e}, 0x19},
	{OV8865_8BIT, {0x371f}, 0x40},
	{OV8865_8BIT, {0x3720}, 0x05},
	{OV8865_8BIT, {0x3721}, 0x05},
	{OV8865_8BIT, {0x3724}, 0x02},
	{OV8865_8BIT, {0x3725}, 0x02},
	{OV8865_8BIT, {0x3726}, 0x06},
	{OV8865_8BIT, {0x3728}, 0x05},
	{OV8865_8BIT, {0x3729}, 0x02},
	{OV8865_8BIT, {0x372a}, 0x03},
	{OV8865_8BIT, {0x372b}, 0x53},
	{OV8865_8BIT, {0x372c}, 0xa3},
	{OV8865_8BIT, {0x372d}, 0x53},
	{OV8865_8BIT, {0x372e}, 0x06},
	{OV8865_8BIT, {0x372f}, 0x10},
	{OV8865_8BIT, {0x3730}, 0x01},
	{OV8865_8BIT, {0x3731}, 0x06},
	{OV8865_8BIT, {0x3732}, 0x14},
	{OV8865_8BIT, {0x3736}, 0x20},
	{OV8865_8BIT, {0x373a}, 0x02},
	{OV8865_8BIT, {0x373b}, 0x0c},
	{OV8865_8BIT, {0x373c}, 0x0a},
	{OV8865_8BIT, {0x373e}, 0x03},
	{OV8865_8BIT, {0x375a}, 0x06},
	{OV8865_8BIT, {0x375b}, 0x13},
	{OV8865_8BIT, {0x375d}, 0x02},
	{OV8865_8BIT, {0x375f}, 0x14},
	{OV8865_8BIT, {0x3767}, 0x1c},
	{OV8865_8BIT, {0x3772}, 0x23},
	{OV8865_8BIT, {0x3773}, 0x02},
	{OV8865_8BIT, {0x3774}, 0x16},
	{OV8865_8BIT, {0x3775}, 0x12},
	{OV8865_8BIT, {0x3776}, 0x08},
	{OV8865_8BIT, {0x37a0}, 0x44},
	{OV8865_8BIT, {0x37a1}, 0x3d},
	{OV8865_8BIT, {0x37a2}, 0x3d},
	{OV8865_8BIT, {0x37a3}, 0x01},
	{OV8865_8BIT, {0x37a5}, 0x08},
	{OV8865_8BIT, {0x37a7}, 0x44},
	{OV8865_8BIT, {0x37a8}, 0x58},
	{OV8865_8BIT, {0x37a9}, 0x58},
	{OV8865_8BIT, {0x37aa}, 0x44},
	{OV8865_8BIT, {0x37ab}, 0x2e},
	{OV8865_8BIT, {0x37ac}, 0x2e},
	{OV8865_8BIT, {0x37ad}, 0x33},
	{OV8865_8BIT, {0x37ae}, 0x0d},
	{OV8865_8BIT, {0x37af}, 0x0d},
	{OV8865_8BIT, {0x37b3}, 0x42},
	{OV8865_8BIT, {0x37b4}, 0x42},
	{OV8865_8BIT, {0x37b5}, 0x33},
	{OV8865_8BIT, {0x3808}, 0x04},
	{OV8865_8BIT, {0x3809}, 0xc0},
	{OV8865_8BIT, {0x380a}, 0x02},
	{OV8865_8BIT, {0x380b}, 0xe0},
	{OV8865_8BIT, {0x380c}, 0x07},
	{OV8865_8BIT, {0x380d}, 0x1a},
	{OV8865_8BIT, {0x380e}, 0x05},
	{OV8865_8BIT, {0x380f}, 0x28},
	{OV8865_8BIT, {0x3813}, 0x04},
	{OV8865_8BIT, {0x3814}, 0x03},
	{OV8865_8BIT, {0x3821}, 0x61},
	{OV8865_8BIT, {0x382a}, 0x03},
	{OV8865_8BIT, {0x3830}, 0x08},
	{OV8865_8BIT, {0x3836}, 0x02},
	{OV8865_8BIT, {0x3846}, 0x88},
	{OV8865_8BIT, {0x3f08}, 0x0b},
	{OV8865_8BIT, {0x4001}, 0x14},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x02},
	{OV8865_8BIT, {0x4023}, 0x96},
	{OV8865_8BIT, {0x4024}, 0x04},
	{OV8865_8BIT, {0x4025}, 0x94},
	{OV8865_8BIT, {0x4026}, 0x04},
	{OV8865_8BIT, {0x4027}, 0xc4},
	{OV8865_8BIT, {0x4500}, 0x40},
	{OV8865_8BIT, {0x4601}, 0x74},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_1632x1224_30fps[] = {
	/* 1632x1224_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x09},
	{OV8865_8BIT, {0x3501}, 0x5c},
	{OV8865_8BIT, {0x3502}, 0x00},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x24},
	{OV8865_8BIT, {0x3701}, 0x0c},
	{OV8865_8BIT, {0x3702}, 0x28},
	{OV8865_8BIT, {0x3703}, 0x19},
	{OV8865_8BIT, {0x3704}, 0x14},
	{OV8865_8BIT, {0x3706}, 0x38},
	{OV8865_8BIT, {0x3707}, 0x04},
	{OV8865_8BIT, {0x3708}, 0x24},
	{OV8865_8BIT, {0x3709}, 0x40},
	{OV8865_8BIT, {0x370a}, 0x00},
	{OV8865_8BIT, {0x370b}, 0xb8},
	{OV8865_8BIT, {0x370c}, 0x04},
	{OV8865_8BIT, {0x3718}, 0x12},
	{OV8865_8BIT, {0x3712}, 0x42},
	{OV8865_8BIT, {0x371e}, 0x19},
	{OV8865_8BIT, {0x371f}, 0x40},
	{OV8865_8BIT, {0x3720}, 0x05},
	{OV8865_8BIT, {0x3721}, 0x05},
	{OV8865_8BIT, {0x3724}, 0x02},
	{OV8865_8BIT, {0x3725}, 0x02},
	{OV8865_8BIT, {0x3726}, 0x06},
	{OV8865_8BIT, {0x3728}, 0x05},
	{OV8865_8BIT, {0x3729}, 0x02},
	{OV8865_8BIT, {0x372a}, 0x03},
	{OV8865_8BIT, {0x372b}, 0x53},
	{OV8865_8BIT, {0x372c}, 0xa3},
	{OV8865_8BIT, {0x372d}, 0x53},
	{OV8865_8BIT, {0x372e}, 0x06},
	{OV8865_8BIT, {0x372f}, 0x10},
	{OV8865_8BIT, {0x3730}, 0x01},
	{OV8865_8BIT, {0x3731}, 0x06},
	{OV8865_8BIT, {0x3732}, 0x14},
	{OV8865_8BIT, {0x3736}, 0x20},
	{OV8865_8BIT, {0x373a}, 0x02},
	{OV8865_8BIT, {0x373b}, 0x0c},
	{OV8865_8BIT, {0x373c}, 0x0a},
	{OV8865_8BIT, {0x373e}, 0x03},
	{OV8865_8BIT, {0x375a}, 0x06},
	{OV8865_8BIT, {0x375b}, 0x13},
	{OV8865_8BIT, {0x375d}, 0x02},
	{OV8865_8BIT, {0x375f}, 0x14},
	{OV8865_8BIT, {0x3767}, 0x1c},
	{OV8865_8BIT, {0x3772}, 0x23},
	{OV8865_8BIT, {0x3773}, 0x02},
	{OV8865_8BIT, {0x3774}, 0x16},
	{OV8865_8BIT, {0x3775}, 0x12},
	{OV8865_8BIT, {0x3776}, 0x08},
	{OV8865_8BIT, {0x37a0}, 0x44},
	{OV8865_8BIT, {0x37a1}, 0x3d},
	{OV8865_8BIT, {0x37a2}, 0x3d},
	{OV8865_8BIT, {0x37a3}, 0x01},
	{OV8865_8BIT, {0x37a5}, 0x08},
	{OV8865_8BIT, {0x37a7}, 0x44},
	{OV8865_8BIT, {0x37a8}, 0x58},
	{OV8865_8BIT, {0x37a9}, 0x58},
	{OV8865_8BIT, {0x37aa}, 0x44},
	{OV8865_8BIT, {0x37ab}, 0x2e},
	{OV8865_8BIT, {0x37ac}, 0x2e},
	{OV8865_8BIT, {0x37ad}, 0x33},
	{OV8865_8BIT, {0x37ae}, 0x0d},
	{OV8865_8BIT, {0x37af}, 0x0d},
	{OV8865_8BIT, {0x37b3}, 0x42},
	{OV8865_8BIT, {0x37b4}, 0x42},
	{OV8865_8BIT, {0x37b5}, 0x33},
	{OV8865_8BIT, {0x3808}, 0x06},
	{OV8865_8BIT, {0x3809}, 0x60},
	{OV8865_8BIT, {0x380a}, 0x04},
	{OV8865_8BIT, {0x380b}, 0xc8},
	{OV8865_8BIT, {0x380c}, 0x06},
	{OV8865_8BIT, {0x380d}, 0x42},
	{OV8865_8BIT, {0x380e}, 0x05},
	{OV8865_8BIT, {0x380f}, 0xda},
	{OV8865_8BIT, {0x3813}, 0x04},
	{OV8865_8BIT, {0x3814}, 0x03},
	{OV8865_8BIT, {0x3821}, 0x61},
	{OV8865_8BIT, {0x382a}, 0x03},
	{OV8865_8BIT, {0x3830}, 0x08},
	{OV8865_8BIT, {0x3836}, 0x02},
	{OV8865_8BIT, {0x3846}, 0x88},
	{OV8865_8BIT, {0x3f08}, 0x0b},
	{OV8865_8BIT, {0x4001}, 0x14},
	{OV8865_8BIT, {0x4020}, 0x01},
	{OV8865_8BIT, {0x4021}, 0x20},
	{OV8865_8BIT, {0x4022}, 0x01},
	{OV8865_8BIT, {0x4023}, 0x9f},
	{OV8865_8BIT, {0x4024}, 0x03},
	{OV8865_8BIT, {0x4025}, 0xe0},
	{OV8865_8BIT, {0x4026}, 0x04},
	{OV8865_8BIT, {0x4027}, 0x5f},
	{OV8865_8BIT, {0x4500}, 0x40},
	{OV8865_8BIT, {0x4601}, 0x74},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_1936x1096_21fps[] = {
	/* 1936x1096_21fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x44},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x07},
	{OV8865_8BIT, {0x3809}, 0x90},
	{OV8865_8BIT, {0x380a}, 0x04},
	{OV8865_8BIT, {0x380b}, 0x48},
	{OV8865_8BIT, {0x380c}, 0x0f},
	{OV8865_8BIT, {0x380d}, 0x98},
	{OV8865_8BIT, {0x380e}, 0x06}, //04
	{OV8865_8BIT, {0x380f}, 0xb5}, //b2
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x05},
	{OV8865_8BIT, {0x4023}, 0x65},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0x63},
	{OV8865_8BIT, {0x4026}, 0x07},
	{OV8865_8BIT, {0x4027}, 0x93},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_1936x1096_30fps[] = {
	/* 1936x1096_30fps */

	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x44},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x07},
	{OV8865_8BIT, {0x3809}, 0x90},
	{OV8865_8BIT, {0x380a}, 0x04},
	{OV8865_8BIT, {0x380b}, 0x48},
	{OV8865_8BIT, {0x380c}, 0x0f},
	{OV8865_8BIT, {0x380d}, 0x98},
	{OV8865_8BIT, {0x380e}, 0x04},
	{OV8865_8BIT, {0x380f}, 0xb2},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x05},
	{OV8865_8BIT, {0x4023}, 0x65},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0x63},
	{OV8865_8BIT, {0x4026}, 0x07},
	{OV8865_8BIT, {0x4027}, 0x93},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_2064x1552_21fps[] = {
	/* 2064x1552_21fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x08},
	{OV8865_8BIT, {0x3809}, 0x10},
	{OV8865_8BIT, {0x380a}, 0x06},
	{OV8865_8BIT, {0x380b}, 0x10},
	{OV8865_8BIT, {0x380c}, 0x0b},
	{OV8865_8BIT, {0x380d}, 0xf0},
	{OV8865_8BIT, {0x380e}, 0x08},   //;06
	{OV8865_8BIT, {0x380f}, 0xc2},   //;22
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x05},
	{OV8865_8BIT, {0x4023}, 0xe6},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xe4},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0x14},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};


static const struct ov8865_reg ov8865_2064x1552_30fps[] = {
	/* 2064x1552_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x08},
	{OV8865_8BIT, {0x3809}, 0x10},
	{OV8865_8BIT, {0x380a}, 0x06},
	{OV8865_8BIT, {0x380b}, 0x10},
	{OV8865_8BIT, {0x380c}, 0x0b},
	{OV8865_8BIT, {0x380d}, 0xf0},
	{OV8865_8BIT, {0x380e}, 0x06},
	{OV8865_8BIT, {0x380f}, 0x22},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x05},
	{OV8865_8BIT, {0x4023}, 0xe6},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xe4},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0x14},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_2576x1936_10fps[] = {
	/* 2576x1936_10fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x01},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0a},
	{OV8865_8BIT, {0x3809}, 0x10},
	{OV8865_8BIT, {0x380a}, 0x07},
	{OV8865_8BIT, {0x380b}, 0x90},
	{OV8865_8BIT, {0x380c}, 0x10},
	{OV8865_8BIT, {0x380d}, 0x3a},
	{OV8865_8BIT, {0x380e}, 0x0d},
	{OV8865_8BIT, {0x380f}, 0x88},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x07},
	{OV8865_8BIT, {0x4023}, 0xe6},
	{OV8865_8BIT, {0x4024}, 0x09},
	{OV8865_8BIT, {0x4025}, 0xe4},
	{OV8865_8BIT, {0x4026}, 0x0a},
	{OV8865_8BIT, {0x4027}, 0x14},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_2576x1936_21fps[] = {
	/* 2576x1936_21fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0a},
	{OV8865_8BIT, {0x3809}, 0x10},
	{OV8865_8BIT, {0x380a}, 0x07},
	{OV8865_8BIT, {0x380b}, 0x90},
	{OV8865_8BIT, {0x380c}, 0x08},
	{OV8865_8BIT, {0x380d}, 0xa2},
	{OV8865_8BIT, {0x380e}, 0x0c}, //;08
	{OV8865_8BIT, {0x380f}, 0x1e}, //;7c
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x07},
	{OV8865_8BIT, {0x4023}, 0xe6},
	{OV8865_8BIT, {0x4024}, 0x09},
	{OV8865_8BIT, {0x4025}, 0xe4},
	{OV8865_8BIT, {0x4026}, 0x0a},
	{OV8865_8BIT, {0x4027}, 0x14},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};
/*****************************OV8865 PREVIEW********************************/

static struct ov8865_reg const ov8865_2576x1936_30fps[] = {
	/* 2576x1936_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0a},
	{OV8865_8BIT, {0x3809}, 0x10},
	{OV8865_8BIT, {0x380a}, 0x07},
	{OV8865_8BIT, {0x380b}, 0x90},
	{OV8865_8BIT, {0x380c}, 0x08},
	{OV8865_8BIT, {0x380d}, 0xa2},
	{OV8865_8BIT, {0x380e}, 0x08},
	{OV8865_8BIT, {0x380f}, 0x7c},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x00},
	{OV8865_8BIT, {0x4021}, 0x00},
	{OV8865_8BIT, {0x4022}, 0x07},
	{OV8865_8BIT, {0x4023}, 0xe6},
	{OV8865_8BIT, {0x4024}, 0x09},
	{OV8865_8BIT, {0x4025}, 0xe4},
	{OV8865_8BIT, {0x4026}, 0x0a},
	{OV8865_8BIT, {0x4027}, 0x14},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static struct ov8865_reg const ov8865_3280x1852_10fps[] = {
	/* 3280x1852_10fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x72},
	{OV8865_8BIT, {0x3502}, 0x20},
	{OV8865_8BIT, {0x3508}, 0x01},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0c},
	{OV8865_8BIT, {0x3809}, 0xd0},
	{OV8865_8BIT, {0x380a}, 0x07},
	{OV8865_8BIT, {0x380b}, 0x3c},
	{OV8865_8BIT, {0x380c}, 0x12},
	{OV8865_8BIT, {0x380d}, 0x54},
	{OV8865_8BIT, {0x380e}, 0x0b},
	{OV8865_8BIT, {0x380f}, 0xfc},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x02},
	{OV8865_8BIT, {0x4021}, 0x40},
	{OV8865_8BIT, {0x4022}, 0x03},
	{OV8865_8BIT, {0x4023}, 0x3f},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xc0},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0xbf},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static struct ov8865_reg const ov8865_3280x1852_21fps[] = {
	/* 3280x1852_21fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x72},
	{OV8865_8BIT, {0x3502}, 0x20},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0c},
	{OV8865_8BIT, {0x3809}, 0xd0},
	{OV8865_8BIT, {0x380a}, 0x07},
	{OV8865_8BIT, {0x380b}, 0x3c},
	{OV8865_8BIT, {0x380c}, 0x0b},//;0a
	{OV8865_8BIT, {0x380d}, 0x3e},//;00
	{OV8865_8BIT, {0x380e}, 0x09},//;07
	{OV8865_8BIT, {0x380f}, 0x4e},//;52
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x02},
	{OV8865_8BIT, {0x4021}, 0x40},
	{OV8865_8BIT, {0x4022}, 0x03},
	{OV8865_8BIT, {0x4023}, 0x3f},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xc0},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0xbf},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};


static const struct ov8865_reg ov8865_3280x1852_30fps[] = {
	/* 3280x1852_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x72},
	{OV8865_8BIT, {0x3502}, 0x20},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0c},
	{OV8865_8BIT, {0x3809}, 0xd0},
	{OV8865_8BIT, {0x380a}, 0x07},
	{OV8865_8BIT, {0x380b}, 0x3c},
	{OV8865_8BIT, {0x380c}, 0x09},
	{OV8865_8BIT, {0x380d}, 0x20},
	{OV8865_8BIT, {0x380e}, 0x08},
	{OV8865_8BIT, {0x380f}, 0x00},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x02},
	{OV8865_8BIT, {0x4021}, 0x40},
	{OV8865_8BIT, {0x4022}, 0x03},
	{OV8865_8BIT, {0x4023}, 0x3f},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xc0},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0xbf},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_3280x2464_21fps[] = {
	/* 3280x2464_21fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0c},
	{OV8865_8BIT, {0x3809}, 0xd0},
	{OV8865_8BIT, {0x380a}, 0x09},
	{OV8865_8BIT, {0x380b}, 0xa0},
	{OV8865_8BIT, {0x380c}, 0x08},//;07
	{OV8865_8BIT, {0x380d}, 0xd2},//;90
	{OV8865_8BIT, {0x380e}, 0x0b},//;09
	{OV8865_8BIT, {0x380f}, 0xda},//;b2
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x02},
	{OV8865_8BIT, {0x4021}, 0x40},
	{OV8865_8BIT, {0x4022}, 0x03},
	{OV8865_8BIT, {0x4023}, 0x3f},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xc0},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0xbf},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_3280x2464_10fps[] = {
	/* 3280x2464_10fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0c},
	{OV8865_8BIT, {0x3809}, 0xd0},
	{OV8865_8BIT, {0x380a}, 0x09},
	{OV8865_8BIT, {0x380b}, 0xa0},
	{OV8865_8BIT, {0x380c}, 0x12},//;07
	{OV8865_8BIT, {0x380d}, 0x54},//;90
	{OV8865_8BIT, {0x380e}, 0x0b},//;09
	{OV8865_8BIT, {0x380f}, 0xda},//;b2
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x02},
	{OV8865_8BIT, {0x4021}, 0x40},
	{OV8865_8BIT, {0x4022}, 0x03},
	{OV8865_8BIT, {0x4023}, 0x3f},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xc0},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0xbf},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static const struct ov8865_reg ov8865_3280x2464_30fps[] = {
	/* 3280x2464_30fps */
	{OV8865_8BIT, {0x0100}, 0x00},
	{OV8865_8BIT, {0x030f}, 0x04},
	{OV8865_8BIT, {0x3501}, 0x98},
	{OV8865_8BIT, {0x3502}, 0x60},
	{OV8865_8BIT, {0x3508}, 0x02},
	{OV8865_8BIT, {0x3700}, 0x48},
	{OV8865_8BIT, {0x3701}, 0x18},
	{OV8865_8BIT, {0x3702}, 0x50},
	{OV8865_8BIT, {0x3703}, 0x32},
	{OV8865_8BIT, {0x3704}, 0x28},
	{OV8865_8BIT, {0x3706}, 0x70},
	{OV8865_8BIT, {0x3707}, 0x08},
	{OV8865_8BIT, {0x3708}, 0x48},
	{OV8865_8BIT, {0x3709}, 0x80},
	{OV8865_8BIT, {0x370a}, 0x01},
	{OV8865_8BIT, {0x370b}, 0x70},
	{OV8865_8BIT, {0x370c}, 0x07},
	{OV8865_8BIT, {0x3718}, 0x14},
	{OV8865_8BIT, {0x3712}, 0x44},
	{OV8865_8BIT, {0x371e}, 0x31},
	{OV8865_8BIT, {0x371f}, 0x7f},
	{OV8865_8BIT, {0x3720}, 0x0a},
	{OV8865_8BIT, {0x3721}, 0x0a},
	{OV8865_8BIT, {0x3724}, 0x04},
	{OV8865_8BIT, {0x3725}, 0x04},
	{OV8865_8BIT, {0x3726}, 0x0c},
	{OV8865_8BIT, {0x3728}, 0x0a},
	{OV8865_8BIT, {0x3729}, 0x03},
	{OV8865_8BIT, {0x372a}, 0x06},
	{OV8865_8BIT, {0x372b}, 0xa6},
	{OV8865_8BIT, {0x372c}, 0xa6},
	{OV8865_8BIT, {0x372d}, 0xa6},
	{OV8865_8BIT, {0x372e}, 0x0c},
	{OV8865_8BIT, {0x372f}, 0x20},
	{OV8865_8BIT, {0x3730}, 0x02},
	{OV8865_8BIT, {0x3731}, 0x0c},
	{OV8865_8BIT, {0x3732}, 0x28},
	{OV8865_8BIT, {0x3736}, 0x30},
	{OV8865_8BIT, {0x373a}, 0x04},
	{OV8865_8BIT, {0x373b}, 0x18},
	{OV8865_8BIT, {0x373c}, 0x14},
	{OV8865_8BIT, {0x373e}, 0x06},
	{OV8865_8BIT, {0x375a}, 0x0c},
	{OV8865_8BIT, {0x375b}, 0x26},
	{OV8865_8BIT, {0x375d}, 0x04},
	{OV8865_8BIT, {0x375f}, 0x28},
	{OV8865_8BIT, {0x3767}, 0x1e},
	{OV8865_8BIT, {0x3772}, 0x46},
	{OV8865_8BIT, {0x3773}, 0x04},
	{OV8865_8BIT, {0x3774}, 0x2c},
	{OV8865_8BIT, {0x3775}, 0x13},
	{OV8865_8BIT, {0x3776}, 0x10},
	{OV8865_8BIT, {0x37a0}, 0x88},
	{OV8865_8BIT, {0x37a1}, 0x7a},
	{OV8865_8BIT, {0x37a2}, 0x7a},
	{OV8865_8BIT, {0x37a3}, 0x02},
	{OV8865_8BIT, {0x37a5}, 0x09},
	{OV8865_8BIT, {0x37a7}, 0x88},
	{OV8865_8BIT, {0x37a8}, 0xb0},
	{OV8865_8BIT, {0x37a9}, 0xb0},
	{OV8865_8BIT, {0x37aa}, 0x88},
	{OV8865_8BIT, {0x37ab}, 0x5c},
	{OV8865_8BIT, {0x37ac}, 0x5c},
	{OV8865_8BIT, {0x37ad}, 0x55},
	{OV8865_8BIT, {0x37ae}, 0x19},
	{OV8865_8BIT, {0x37af}, 0x19},
	{OV8865_8BIT, {0x37b3}, 0x84},
	{OV8865_8BIT, {0x37b4}, 0x84},
	{OV8865_8BIT, {0x37b5}, 0x66},
	{OV8865_8BIT, {0x3808}, 0x0c},
	{OV8865_8BIT, {0x3809}, 0xd0},
	{OV8865_8BIT, {0x380a}, 0x09},
	{OV8865_8BIT, {0x380b}, 0xa0},
	{OV8865_8BIT, {0x380c}, 0x07},
	{OV8865_8BIT, {0x380d}, 0x90},
	{OV8865_8BIT, {0x380e}, 0x09},
	{OV8865_8BIT, {0x380f}, 0xb6},
	{OV8865_8BIT, {0x3813}, 0x02},
	{OV8865_8BIT, {0x3814}, 0x01},
	{OV8865_8BIT, {0x3821}, 0x40},
	{OV8865_8BIT, {0x382a}, 0x01},
	{OV8865_8BIT, {0x3830}, 0x04},
	{OV8865_8BIT, {0x3836}, 0x01},
	{OV8865_8BIT, {0x3846}, 0x48},
	{OV8865_8BIT, {0x3f08}, 0x16},
	{OV8865_8BIT, {0x4001}, 0x04},
	{OV8865_8BIT, {0x4020}, 0x02},
	{OV8865_8BIT, {0x4021}, 0x40},
	{OV8865_8BIT, {0x4022}, 0x03},
	{OV8865_8BIT, {0x4023}, 0x3f},
	{OV8865_8BIT, {0x4024}, 0x07},
	{OV8865_8BIT, {0x4025}, 0xc0},
	{OV8865_8BIT, {0x4026}, 0x08},
	{OV8865_8BIT, {0x4027}, 0xbf},
	{OV8865_8BIT, {0x4500}, 0x68},
	{OV8865_8BIT, {0x4601}, 0x10},

	{ OV8865_TOK_TERM, {0}, 0}
};

static struct ov8865_resolution ov8865_res_preview[] = {
	{
		 .desc = "ov8865_896x736_30fps",
		 .width = 896,
		 .height = 736,
		 .used = 0,
		 .regs = ov8865_896x736_30fps,
		 .bin_factor_x = 1,
		 .bin_factor_y = 1,
		 .skip_frames = 0,
		 .fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x6e6, /* 1766 */
				 .lines_per_frame = 0x550, /* 1360 */
			},
			{
			}
		},
	},
	{
		 .desc = "ov8865_1632x1224_30fps",
		 .width = 1632,
		 .height = 1224,
		 .used = 0,
		 .regs = ov8865_1632x1224_30fps,
		 .bin_factor_x = 1,
		 .bin_factor_y = 1,
		 .skip_frames = 0,
		 .fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0642, /* 1602 */
				.lines_per_frame = 0x05da,   /* 1498 */
			},
			{
			}
		}
	},
      {
		.desc = "ov8865_1936x1096_30fps",
		.width = 1936,
		.height = 1096,
		.regs = ov8865_1936x1096_30fps,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.skip_frames = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0f98, /* 3992 */
				 .lines_per_frame = 0x04b2, /* 1202 */
			},
			{
			}
		},
	},
	{
		 .desc = "ov8865_3280x1852_30fps",
		 .width = 3280,
		 .height = 1852,
		 .used = 0,
		 .regs = ov8865_3280x1852_30fps,
		 .bin_factor_x = 0,
		 .bin_factor_y = 0,
		 .skip_frames = 0,
		 .fps_options =  {
			{
				.fps = 30,
				.pixels_per_line = 0x0920, /* 2336*/
				.lines_per_frame = 0x0800, /* 2048 */
			},
			{
			}
		}
	},
	{
		 .desc = "ov8865_3280x2464_30fps",
		 .width = 3280,
		 .height = 2464,
		 .used = 0,
		 .regs = ov8865_3280x2464_30fps,
		 .bin_factor_x = 0,
		 .bin_factor_y = 0,
		 .skip_frames = 0,
		 .fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0790, /* 1936*/
				.lines_per_frame = 0x09b6, /* 2486 */
			},
			{
			}
		},
	},
};

static struct ov8865_resolution ov8865_res_still[] = {
#if 0
	{
		 .desc = "ov8865_2576x1936_10fps",
		 .width = 2576,
		 .height = 1936,
		 .used = 0,
		 .regs = ov8865_2576x1936_10fps,
		 .bin_factor_x = 0,
		 .bin_factor_y = 0,
		 .skip_frames = 1,
		 .fps_options =  {
			{
				.fps = 10,
				.pixels_per_line = 0x103a, /* 4154 */
				.lines_per_frame = 0x0d88, /* 3464 */
			},
			{
			}
		},
	},
#endif

        {
                 .desc = "ov8865_896x736_30fps",
                 .width = 896,
                 .height = 736,
                 .used = 0,
                 .regs = ov8865_896x736_30fps,
                 .bin_factor_x = 1,
                 .bin_factor_y = 1,
                 .skip_frames = 0,
                 .fps_options = {
                        {
                                 .fps = 30,
                                 .pixels_per_line = 0x6e6, /* 1766 */
                                 .lines_per_frame = 0x550, /* 1360 */
                        },
                        {
                        }
                },
        },
       {
                 .desc = "ov8865_1632x1224_30fps",
                 .width = 1632,
                 .height = 1224,
                 .used = 0,
                 .regs = ov8865_1632x1224_30fps,
                 .bin_factor_x = 1,
                 .bin_factor_y = 1,
                 .skip_frames = 1,
                 .fps_options = {
                        {
                                .fps = 30,
                                .pixels_per_line = 0x0642, /* 1602 */
                                .lines_per_frame = 0x05da,   /* 1498 */
                        },
                        {
                        }
                }
        },
      {
		.desc = "ov8865_1936x1096_30fps",
		.width = 1936,
		.height = 1096,
		.regs = ov8865_1936x1096_30fps,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.skip_frames = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0f98, /* 3992 */
				 .lines_per_frame = 0x04b2, /* 1202 */
			},
			{
			}
		},
	},
	{
		 .desc = "ov8865_3280x1852_10fps",
		 .width = 3280,
		 .height = 1852,
		 .used = 0,
		 .regs = ov8865_3280x1852_10fps,
		 .bin_factor_x = 0,
		 .bin_factor_y = 0,
		 .skip_frames = 1,
		 .fps_options =  {
			{
				.fps = 10,
				.pixels_per_line = 0x1254, /* 4692 */
				.lines_per_frame = 0x0bfc, /* 3068 */
			},
			{
			}
		},
	},
	{
		 .desc = "ov8865_3280x2464_10fps",
		 .width = 3280,
		 .height = 2464,
		 .used = 0,
		 .regs = ov8865_3280x2464_10fps,
		 .bin_factor_x = 0,
		 .bin_factor_y = 0,
		 .skip_frames = 0,
		 .fps_options = {
			{
				.fps = 10,
				.pixels_per_line = 0x1254/* 4692*/, /* 2258 */
				.lines_per_frame = 0x0bda, /* 3034 */
			},
			{
			}
		},
	},
};

static struct ov8865_resolution ov8865_res_video[] = {
	{
		 .desc = "ov8865_896x736_30fps",
		 .width = 896,
		 .height = 736,
		 .used = 0,
		 .regs = ov8865_896x736_30fps,
		 .bin_factor_x = 1,
		 .bin_factor_y = 1,
		 .skip_frames = 0,
		 .fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x6e6, /* 1766 */
				 .lines_per_frame = 0x550, /* 1360 */
			},
			{
			}
		},
	},
#if 0
	{
		 .desc = "ov8865_1216x736_30fps",
		 .width = 1216,
		 .height = 736,
		 .used = 0,
		 .regs = ov8865_1216x736_30fps,
		 .bin_factor_x = 1,
		 .bin_factor_y = 1,
		 .skip_frames = 0,
		 .fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x071a, /* 1818 */
				 .lines_per_frame = 0x0528, /* 1320 */
			},
			{
			}
		},
	},
#endif
	{
		 .desc = "ov8865_1632x1224_30fps",
		 .width = 1632,
		 .height = 1224,
		 .used = 0,
		 .regs = ov8865_1632x1224_30fps,
		 .bin_factor_x = 1,
		 .bin_factor_y = 1,
		 .skip_frames = 0,
		 .fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0642, /* 1602 */
				 .lines_per_frame = 0x05da, /* 1498 */
			},
			{
			}
		},
	},

	{
		.desc = "ov8865_1936x1096_30fps",
		.width = 1936,
		.height = 1096,
		.regs = ov8865_1936x1096_30fps,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.skip_frames = 0,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 0x0f98, /* 3992 */
				 .lines_per_frame = 0x04b2, /* 1202 */
			},
			{
			}
		},
	},

        {
                 .desc = "ov8865_3280x1852_30fps",
                 .width = 3280,
                 .height = 1852,
                 .used = 0,
                 .regs = ov8865_3280x1852_30fps,
                 .bin_factor_x = 0,
                 .bin_factor_y = 0,
                 .skip_frames = 0,
                 .fps_options =  {
                        {
                                .fps = 30,
                                .pixels_per_line = 0x0920, /* 2336*/
                                .lines_per_frame = 0x0800, /* 2048 */
                        },
                        {
                        }
                }
        },
        {
                 .desc = "ov8865_3280x2464_30fps",
                 .width = 3280,
                 .height = 2464,
                 .used = 0,
                 .regs = ov8865_3280x2464_30fps,
                 .bin_factor_x = 0,
                 .bin_factor_y = 0,
                 .skip_frames = 0,
                 .fps_options = {
                        {
                                .fps = 30,
                                .pixels_per_line = 0x0790, /* 1936*/
                                .lines_per_frame = 0x09b6, /* 2486 */
                        },
                        {
                        }
                },
        },

};

static int
ov8865_read_reg(struct i2c_client *client, u16 len, u16 reg, u16 *val);

static int
ov8865_write_reg(struct i2c_client *client, u16 data_length, u16 reg, u16 val);

#endif
