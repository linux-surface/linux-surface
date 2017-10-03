/*
 * Precise Touch HECI Message
 *
 * Copyright (c) 2013-2016, Intel Corporation.
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

#ifndef _IPTS_MEI_MSGS_H_
#define _IPTS_MEI_MSGS_H_

#include "ipts-sensor-regs.h"

#pragma pack(1)


// Initial protocol version
#define TOUCH_HECI_CLIENT_PROTOCOL_VERSION      10

// GUID that identifies the Touch HECI client.
#define TOUCH_HECI_CLIENT_GUID  \
            {0x3e8d0870, 0x271a, 0x4208, {0x8e, 0xb5, 0x9a, 0xcb, 0x94, 0x02, 0xae, 0x04}}


// define C_ASSERT macro to check structure size and fail compile for unexpected mismatch
#ifndef C_ASSERT
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#endif


// General Type Defines for compatibility with HID driver and BIOS
#ifndef BIT0
#define BIT0 1
#endif
#ifndef BIT1
#define BIT1 2
#endif
#ifndef BIT2
#define BIT2 4
#endif


#define TOUCH_SENSOR_GET_DEVICE_INFO_CMD        0x00000001
#define TOUCH_SENSOR_GET_DEVICE_INFO_RSP        0x80000001


#define TOUCH_SENSOR_SET_MODE_CMD               0x00000002
#define TOUCH_SENSOR_SET_MODE_RSP               0x80000002


#define TOUCH_SENSOR_SET_MEM_WINDOW_CMD         0x00000003
#define TOUCH_SENSOR_SET_MEM_WINDOW_RSP         0x80000003


#define TOUCH_SENSOR_QUIESCE_IO_CMD             0x00000004
#define TOUCH_SENSOR_QUIESCE_IO_RSP             0x80000004


#define TOUCH_SENSOR_HID_READY_FOR_DATA_CMD     0x00000005
#define TOUCH_SENSOR_HID_READY_FOR_DATA_RSP     0x80000005


#define TOUCH_SENSOR_FEEDBACK_READY_CMD         0x00000006
#define TOUCH_SENSOR_FEEDBACK_READY_RSP         0x80000006


#define TOUCH_SENSOR_CLEAR_MEM_WINDOW_CMD       0x00000007
#define TOUCH_SENSOR_CLEAR_MEM_WINDOW_RSP       0x80000007


#define TOUCH_SENSOR_NOTIFY_DEV_READY_CMD       0x00000008
#define TOUCH_SENSOR_NOTIFY_DEV_READY_RSP       0x80000008


#define TOUCH_SENSOR_SET_POLICIES_CMD           0x00000009
#define TOUCH_SENSOR_SET_POLICIES_RSP           0x80000009


#define TOUCH_SENSOR_GET_POLICIES_CMD           0x0000000A
#define TOUCH_SENSOR_GET_POLICIES_RSP           0x8000000A


#define TOUCH_SENSOR_RESET_CMD                  0x0000000B
#define TOUCH_SENSOR_RESET_RSP                  0x8000000B


#define TOUCH_SENSOR_READ_ALL_REGS_CMD          0x0000000C
#define TOUCH_SENSOR_READ_ALL_REGS_RSP          0x8000000C


#define TOUCH_SENSOR_CMD_ERROR_RSP              0x8FFFFFFF  // M2H: ME sends this message to indicate previous command was unrecognized/unsupported



//*******************************************************************
//
// Touch Sensor Status Codes
//
//*******************************************************************
typedef enum touch_status
{
    TOUCH_STATUS_SUCCESS = 0,               //  0 Requested operation was successful
    TOUCH_STATUS_INVALID_PARAMS,            //  1 Invalid parameter(s) sent
    TOUCH_STATUS_ACCESS_DENIED,             //  2 Unable to validate address range
    TOUCH_STATUS_CMD_SIZE_ERROR,            //  3 HECI message incorrect size for specified command
    TOUCH_STATUS_NOT_READY,                 //  4 Memory window not set or device is not armed for operation
    TOUCH_STATUS_REQUEST_OUTSTANDING,       //  5 There is already an outstanding message of the same type, must wait for response before sending another request of that type
    TOUCH_STATUS_NO_SENSOR_FOUND,           //  6 Sensor could not be found. Either no sensor is connected, the sensor has not yet initialized, or the system is improperly configured.
    TOUCH_STATUS_OUT_OF_MEMORY,             //  7 Not enough memory/storage for requested operation
    TOUCH_STATUS_INTERNAL_ERROR,            //  8 Unexpected error occurred
    TOUCH_STATUS_SENSOR_DISABLED,           //  9 Used in TOUCH_SENSOR_HID_READY_FOR_DATA_RSP to indicate sensor has been disabled or reset and must be reinitialized.
    TOUCH_STATUS_COMPAT_CHECK_FAIL,         // 10 Used to indicate compatibility revision check between sensor and ME failed, or protocol ver between ME/HID/Kernels failed.
    TOUCH_STATUS_SENSOR_EXPECTED_RESET,     // 11 Indicates sensor went through a reset initiated by ME
    TOUCH_STATUS_SENSOR_UNEXPECTED_RESET,   // 12 Indicates sensor went through an unexpected reset
    TOUCH_STATUS_RESET_FAILED,              // 13 Requested sensor reset failed to complete
    TOUCH_STATUS_TIMEOUT,                   // 14 Operation timed out
    TOUCH_STATUS_TEST_MODE_FAIL,            // 15 Test mode pattern did not match expected values
    TOUCH_STATUS_SENSOR_FAIL_FATAL,         // 16 Indicates sensor reported fatal error during reset sequence. Further progress is not possible.
    TOUCH_STATUS_SENSOR_FAIL_NONFATAL,      // 17 Indicates sensor reported non-fatal error during reset sequence. HID/BIOS logs error and attempts to continue.
    TOUCH_STATUS_INVALID_DEVICE_CAPS,       // 18 Indicates sensor reported invalid capabilities, such as not supporting required minimum frequency or I/O mode.
    TOUCH_STATUS_QUIESCE_IO_IN_PROGRESS,    // 19 Indicates that command cannot be complete until ongoing Quiesce I/O flow has completed.
    TOUCH_STATUS_MAX                        // 20 Invalid value, never returned
} touch_status_t;
C_ASSERT(sizeof(touch_status_t) == 4);



//*******************************************************************
//
// Defines for message structures used for Host to ME communication
//
//*******************************************************************


typedef enum touch_sensor_mode
{
    TOUCH_SENSOR_MODE_HID = 0,          // Set mode to HID mode
    TOUCH_SENSOR_MODE_RAW_DATA,         // Set mode to Raw Data mode
    TOUCH_SENSOR_MODE_SENSOR_DEBUG = 4, // Used like TOUCH_SENSOR_MODE_HID but data coming from sensor is not necessarily a HID packet.
    TOUCH_SENSOR_MODE_MAX               // Invalid value
} touch_sensor_mode_t;
C_ASSERT(sizeof(touch_sensor_mode_t) == 4);

typedef struct touch_sensor_set_mode_cmd_data
{
    touch_sensor_mode_t	sensor_mode;     // Indicate desired sensor mode
    u32					Reserved[3];    // For future expansion
} touch_sensor_set_mode_cmd_data_t;
C_ASSERT(sizeof(touch_sensor_set_mode_cmd_data_t) == 16);


#define TOUCH_SENSOR_MAX_DATA_BUFFERS   16
#define TOUCH_HID_2_ME_BUFFER_ID        TOUCH_SENSOR_MAX_DATA_BUFFERS
#define TOUCH_HID_2_ME_BUFFER_SIZE_MAX  1024
#define TOUCH_INVALID_BUFFER_ID         0xFF

typedef struct touch_sensor_set_mem_window_cmd_data
{
    u32  touch_data_buffer_addr_lower[TOUCH_SENSOR_MAX_DATA_BUFFERS];    // Lower 32 bits of Touch Data Buffer physical address. Size of each buffer should be TOUCH_SENSOR_GET_DEVICE_INFO_RSP_DATA.FrameSize
    u32  touch_data_buffer_addr_upper[TOUCH_SENSOR_MAX_DATA_BUFFERS];    // Upper 32 bits of Touch Data Buffer physical address. Size of each buffer should be TOUCH_SENSOR_GET_DEVICE_INFO_RSP_DATA.FrameSize
    u32  tail_offset_addr_lower;                                        // Lower 32 bits of Tail Offset physical address
    u32  tail_offset_addr_upper;                                        // Upper 32 bits of Tail Offset physical address, always 32 bit, increment by WorkQueueItemSize
    u32  doorbell_cookie_addr_lower;                                    // Lower 32 bits of Doorbell register physical address
    u32  doorbell_cookie_addr_upper;                                    // Upper 32 bits of Doorbell register physical address, always 32 bit, increment as integer, rollover to 1
    u32  feedback_buffer_addr_lower[TOUCH_SENSOR_MAX_DATA_BUFFERS];     // Lower 32 bits of Feedback Buffer physical address. Size of each buffer should be TOUCH_SENSOR_GET_DEVICE_INFO_RSP_DATA.FeedbackSize
    u32  feedback_buffer_addr_upper[TOUCH_SENSOR_MAX_DATA_BUFFERS];     // Upper 32 bits of Feedback Buffer physical address. Size of each buffer should be TOUCH_SENSOR_GET_DEVICE_INFO_RSP_DATA.FeedbackSize
    u32  hid2me_buffer_addr_lower;                                      // Lower 32 bits of dedicated HID to ME communication buffer. Size is Hid2MeBufferSize.
    u32  hid2me_buffer_addr_upper;                                      // Upper 32 bits of dedicated HID to ME communication buffer. Size is Hid2MeBufferSize.
    u32  hid2me_buffer_size;                                           // Size in bytes of Hid2MeBuffer, can be no bigger than TOUCH_HID_2_ME_BUFFER_SIZE_MAX
    u8   reserved1;                                                  // For future expansion
    u8   work_queue_item_size;                                          // Size in bytes of the GuC Work Queue Item pointed to by TailOffset
    u16  work_queue_size;                                              // Size in bytes of the entire GuC Work Queue
    u32  reserved[8];                                                // For future expansion
} touch_sensor_set_mem_window_cmd_data_t;
C_ASSERT(sizeof(touch_sensor_set_mem_window_cmd_data_t) == 320);


#define TOUCH_SENSOR_QUIESCE_FLAG_GUC_RESET BIT0   // indicates GuC got reset and ME must re-read GuC data such as TailOffset and Doorbell Cookie values

typedef struct touch_sensor_quiesce_io_cmd_data
{
    u32  quiesce_flags;   // Optionally set TOUCH_SENSOR_QUIESCE_FLAG_GUC_RESET
    u32  reserved[2];
} touch_sensor_quiesce_io_cmd_data_t;
C_ASSERT(sizeof(touch_sensor_quiesce_io_cmd_data_t) == 12);


typedef struct touch_sensor_feedback_ready_cmd_data
{
    u8   feedback_index;  // Index value from 0 to TOUCH_HID_2_ME_BUFFER_ID used to indicate which Feedback Buffer to use. Using special value TOUCH_HID_2_ME_BUFFER_ID
                            // is an indication to ME to get feedback data from the Hid2Me buffer instead of one of the standard Feedback buffers.
    u8   reserved1[3];   // For future expansion
    u32  transaction_id;  // Transaction ID that was originally passed to host in TOUCH_HID_PRIVATE_DATA. Used to track round trip of a given transaction for performance measurements.
    u32  reserved2[2];   // For future expansion
} touch_sensor_feedback_ready_cmd_data_t;
C_ASSERT(sizeof(touch_sensor_feedback_ready_cmd_data_t) == 16);


#define TOUCH_DEFAULT_DOZE_TIMER_SECONDS    30

typedef enum touch_freq_override
{
    TOUCH_FREQ_OVERRIDE_NONE,   // Do not apply any override
    TOUCH_FREQ_OVERRIDE_10MHZ,  // Force frequency to 10MHz (not currently supported)
    TOUCH_FREQ_OVERRIDE_17MHZ,  // Force frequency to 17MHz
    TOUCH_FREQ_OVERRIDE_30MHZ,  // Force frequency to 30MHz
    TOUCH_FREQ_OVERRIDE_50MHZ,  // Force frequency to 50MHz (not currently supported)
    TOUCH_FREQ_OVERRIDE_MAX     // Invalid value
} touch_freq_override_t;
C_ASSERT(sizeof(touch_freq_override_t) == 4);

typedef enum touch_spi_io_mode_override
{
    TOUCH_SPI_IO_MODE_OVERRIDE_NONE,    // Do not apply any override
    TOUCH_SPI_IO_MODE_OVERRIDE_SINGLE,  // Force Single I/O
    TOUCH_SPI_IO_MODE_OVERRIDE_DUAL,    // Force Dual I/O
    TOUCH_SPI_IO_MODE_OVERRIDE_QUAD,    // Force Quad I/O
    TOUCH_SPI_IO_MODE_OVERRIDE_MAX      // Invalid value
} touch_spi_io_mode_override_t;
C_ASSERT(sizeof(touch_spi_io_mode_override_t) == 4);

// Debug Policy bits used by TOUCH_POLICY_DATA.DebugOverride
#define TOUCH_DBG_POLICY_OVERRIDE_STARTUP_TIMER_DIS BIT0    // Disable sensor startup timer
#define TOUCH_DBG_POLICY_OVERRIDE_SYNC_BYTE_DIS     BIT1    // Disable Sync Byte check
#define TOUCH_DBG_POLICY_OVERRIDE_ERR_RESET_DIS     BIT2    // Disable error resets

typedef struct touch_policy_data
{
    u32                      reserved0;          // For future expansion.
    u32                      doze_timer     :16;  // Value in seconds, after which ME will put the sensor into Doze power state if no activity occurs. Set
                                                    // to 0 to disable Doze mode (not recommended). Value will be set to TOUCH_DEFAULT_DOZE_TIMER_SECONDS by
                                                    // default.
    touch_freq_override_t         freq_override  :3;   // Override frequency requested by sensor
    touch_spi_io_mode_override_t  spi_io_override :3;   // Override IO mode requested by sensor
    u32                      reserved1     :10;  // For future expansion
    u32                      reserved2;          // For future expansion
    u32                      debug_override;      // Normally all bits will be zero. Bits will be defined as needed for enabling special debug features
} touch_policy_data_t;
C_ASSERT(sizeof(touch_policy_data_t) == 16);

typedef struct touch_sensor_set_policies_cmd_data
{
    touch_policy_data_t           policy_data;         // Contains the desired policy to be set
} touch_sensor_set_policies_cmd_data_t;
C_ASSERT(sizeof(touch_sensor_set_policies_cmd_data_t) == 16);


typedef enum touch_sensor_reset_type
{
    TOUCH_SENSOR_RESET_TYPE_HARD,   // Hardware Reset using dedicated GPIO pin
    TOUCH_SENSOR_RESET_TYPE_SOFT,   // Software Reset using command written over SPI interface
    TOUCH_SENSOR_RESET_TYPE_MAX     // Invalid value
} touch_sensor_reset_type_t;
C_ASSERT(sizeof(touch_sensor_reset_type_t) == 4);

typedef struct touch_sensor_reset_cmd_data
{
    touch_sensor_reset_type_t reset_type;  // Indicate desired reset type
    u32                  reserved;   // For future expansion
} touch_sensor_reset_cmd_data_t;
C_ASSERT(sizeof(touch_sensor_reset_cmd_data_t) == 8);


//
// Host to ME message
//
typedef struct touch_sensor_msg_h2m
{
    u32  command_code;
    union
    {
        touch_sensor_set_mode_cmd_data_t        set_mode_cmd_data;
        touch_sensor_set_mem_window_cmd_data_t  set_window_cmd_data;
        touch_sensor_quiesce_io_cmd_data_t      quiesce_io_cmd_data;
        touch_sensor_feedback_ready_cmd_data_t  feedback_ready_cmd_data;
        touch_sensor_set_policies_cmd_data_t    set_policies_cmd_data;
        touch_sensor_reset_cmd_data_t           reset_cmd_data;
    } h2m_data;
} touch_sensor_msg_h2m_t;
C_ASSERT(sizeof(touch_sensor_msg_h2m_t) == 324);


//*******************************************************************
//
// Defines for message structures used for ME to Host communication
//
//*******************************************************************

// I/O mode values used by TOUCH_SENSOR_GET_DEVICE_INFO_RSP_DATA.
typedef enum touch_spi_io_mode
{
    TOUCH_SPI_IO_MODE_SINGLE = 0,   // Sensor set for Single I/O SPI
    TOUCH_SPI_IO_MODE_DUAL,         // Sensor set for Dual I/O SPI
    TOUCH_SPI_IO_MODE_QUAD,         // Sensor set for Quad I/O SPI
    TOUCH_SPI_IO_MODE_MAX           // Invalid value
} touch_spi_io_mode_t;
C_ASSERT(sizeof(touch_spi_io_mode_t) == 4);

//
// TOUCH_SENSOR_GET_DEVICE_INFO_RSP code is sent in response to TOUCH_SENSOR_GET_DEVICE_INFO_CMD. This code will be followed
// by TOUCH_SENSOR_GET_DEVICE_INFO_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:               Command was processed successfully and sensor details are reported.
//      TOUCH_STATUS_CMD_SIZE_ERROR:        Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_NO_SENSOR_FOUND:       Sensor has not yet been detected. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_DEVICE_CAPS:   Indicates sensor does not support minimum required Frequency or I/O Mode. ME firmware will choose best possible option for the errant
//                                          field. Caller should attempt to continue.
//      TOUCH_STATUS_COMPAT_CHECK_FAIL:     Indicates TouchIC/ME compatibility mismatch. Caller should attempt to continue.
//
typedef struct touch_sensor_get_device_info_rsp_data
{
    u16				vendor_id;               // Touch Sensor vendor ID
    u16              device_id;               // Touch Sensor device ID
    u32              hw_rev;                  // Touch Sensor Hardware Revision
    u32              fw_rev;                  // Touch Sensor Firmware Revision
    u32              frame_size;              // Max size of one frame returned by Touch IC in bytes. This data will be TOUCH_RAW_DATA_HDR followed
                                                // by a payload. The payload can be raw data or a HID structure depending on mode.
    u32              feedback_size;           // Max size of one Feedback structure in bytes
    touch_sensor_mode_t	sensor_mode;             // Current operating mode of the sensor
    u32              max_touch_points  :8;     // Maximum number of simultaneous touch points that can be reported by sensor
    touch_freq_t          spi_frequency    :8;     // SPI bus Frequency supported by sensor and ME firmware
    touch_spi_io_mode_t   spi_io_mode       :8;     // SPI bus I/O Mode supported by sensor and ME firmware
    u32              reserved0       :8;     // For future expansion
    u8               sensor_minor_eds_rev;      // Minor version number of EDS spec supported by sensor (from Compat Rev ID Reg)
    u8               sensor_major_eds_rev;      // Major version number of EDS spec supported by sensor (from Compat Rev ID Reg)
    u8               me_minor_eds_rev;          // Minor version number of EDS spec supported by ME
    u8               me_major_eds_rev;          // Major version number of EDS spec supported by ME
    u8               sensor_eds_intf_rev;       // EDS Interface Revision Number supported by sensor (from Compat Rev ID Reg)
    u8               me_eds_intf_rev;           // EDS Interface Revision Number supported by ME
    u8               kernel_compat_ver;        // EU Kernel Compatibility Version  (from Compat Rev ID Reg)
    u8               reserved1;              // For future expansion
    u32              reserved2[2];           // For future expansion
} touch_sensor_get_device_info_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_get_device_info_rsp_data_t) == 44);


//
// TOUCH_SENSOR_SET_MODE_RSP code is sent in response to TOUCH_SENSOR_SET_MODE_CMD. This code will be followed
// by TOUCH_SENSOR_SET_MODE_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:           Command was processed successfully and mode was set.
//      TOUCH_STATUS_CMD_SIZE_ERROR:    Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_PARAMS:    Input parameters are out of range.
//
typedef struct touch_sensor_set_mode_rsp_data
{
    u32          reserved[3];    // For future expansion
} touch_sensor_set_mode_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_set_mode_rsp_data_t) == 12);


//
// TOUCH_SENSOR_SET_MEM_WINDOW_RSP code is sent in response to TOUCH_SENSOR_SET_MEM_WINDOW_CMD. This code will be followed
// by TOUCH_SENSOR_SET_MEM_WINDOW_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:           Command was processed successfully and memory window was set.
//      TOUCH_STATUS_CMD_SIZE_ERROR:    Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_PARAMS:    Input parameters are out of range.
//      TOUCH_STATUS_ACCESS_DENIED:     Unable to map host address ranges for DMA.
//      TOUCH_STATUS_OUT_OF_MEMORY:     Unable to allocate enough space for needed buffers.
//
typedef struct touch_sensor_set_mem_window_rsp_data
{
    u32          reserved[3];    // For future expansion
} touch_sensor_set_mem_window_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_set_mem_window_rsp_data_t) == 12);


//
// TOUCH_SENSOR_QUIESCE_IO_RSP code is sent in response to TOUCH_SENSOR_QUIESCE_IO_CMD. This code will be followed
// by TOUCH_SENSOR_QUIESCE_IO_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:                   Command was processed successfully and touch flow has stopped.
//      TOUCH_STATUS_CMD_SIZE_ERROR:            Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_QUIESCE_IO_IN_PROGRESS:    Indicates that Quiesce I/O is already in progress and this command cannot be accepted at this time.
//      TOUCH_STATIS_TIMEOUT:                   Indicates ME timed out waiting for Quiesce I/O flow to complete.
//
typedef struct touch_sensor_quiesce_io_rsp_data
{
    u32          reserved[3];    // For future expansion
} touch_sensor_quiesce_io_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_quiesce_io_rsp_data_t) == 12);


// Reset Reason values used in TOUCH_SENSOR_HID_READY_FOR_DATA_RSP_DATA
typedef enum touch_reset_reason
{
    TOUCH_RESET_REASON_UNKNOWN = 0,         // Reason for sensor reset is not known
    TOUCH_RESET_REASON_FEEDBACK_REQUEST,    // Reset was requested as part of TOUCH_SENSOR_FEEDBACK_READY_CMD
    TOUCH_RESET_REASON_HECI_REQUEST,        // Reset was requested via TOUCH_SENSOR_RESET_CMD
    TOUCH_RESET_REASON_MAX
} touch_reset_reason_t;
C_ASSERT(sizeof(touch_reset_reason_t) == 4);

//
// TOUCH_SENSOR_HID_READY_FOR_DATA_RSP code is sent in response to TOUCH_SENSOR_HID_READY_FOR_DATA_CMD. This code will be followed
// by TOUCH_SENSOR_HID_READY_FOR_DATA_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:                   Command was processed successfully and HID data was sent by DMA. This will only be sent in HID mode.
//      TOUCH_STATUS_CMD_SIZE_ERROR:            Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_REQUEST_OUTSTANDING:       Previous request is still outstanding, ME FW cannot handle another request for the same command.
//      TOUCH_STATUS_NOT_READY:                 Indicates memory window has not yet been set by BIOS/HID.
//      TOUCH_STATUS_SENSOR_DISABLED:           Indicates that ME to HID communication has been stopped either by TOUCH_SENSOR_QUIESCE_IO_CMD or TOUCH_SENSOR_CLEAR_MEM_WINDOW_CMD.
//      TOUCH_STATUS_SENSOR_UNEXPECTED_RESET:   Sensor signaled a Reset Interrupt. ME did not expect this and has no info about why this occurred.
//      TOUCH_STATUS_SENSOR_EXPECTED_RESET:     Sensor signaled a Reset Interrupt. ME either directly requested this reset, or it was expected as part of a defined flow in the EDS.
//      TOUCH_STATUS_QUIESCE_IO_IN_PROGRESS:    Indicates that Quiesce I/O is already in progress and this command cannot be accepted at this time.
//      TOUCH_STATUS_TIMEOUT:                   Sensor did not generate a reset interrupt in the time allotted. Could indicate sensor is not connected or malfunctioning.
//
typedef struct touch_sensor_hid_ready_for_data_rsp_data
{
    u32          data_size;               // Size of the data the ME DMA'd into a RawDataBuffer. Valid only when Status == TOUCH_STATUS_SUCCESS
    u8           touch_data_buffer_index;   // Index to indicate which RawDataBuffer was used. Valid only when Status == TOUCH_STATUS_SUCCESS
    u8           reset_reason;            // If Status is TOUCH_STATUS_SENSOR_EXPECTED_RESET, ME will provide the cause. See TOUCH_RESET_REASON.
    u8           reserved1[2];           // For future expansion
    u32          reserved2[5];           // For future expansion
} touch_sensor_hid_ready_for_data_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_hid_ready_for_data_rsp_data_t) == 28);


//
// TOUCH_SENSOR_FEEDBACK_READY_RSP code is sent in response to TOUCH_SENSOR_FEEDBACK_READY_CMD. This code will be followed
// by TOUCH_SENSOR_FEEDBACK_READY_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:           Command was processed successfully and any feedback or commands were sent to sensor.
//      TOUCH_STATUS_CMD_SIZE_ERROR:    Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_PARAMS:    Input parameters are out of range.
//      TOUCH_STATUS_COMPAT_CHECK_FAIL  Indicates ProtocolVer does not match ME supported version. (non-fatal error)
//      TOUCH_STATUS_INTERNAL_ERROR:    Unexpected error occurred. This should not normally be seen.
//      TOUCH_STATUS_OUT_OF_MEMORY:     Insufficient space to store Calibration Data
//
typedef struct touch_sensor_feedback_ready_rsp_data
{
    u8           feedback_index;  // Index value from 0 to TOUCH_SENSOR_MAX_DATA_BUFFERS used to indicate which Feedback Buffer to use
    u8           reserved1[3];   // For future expansion
    u32          reserved2[6];   // For future expansion
} touch_sensor_feedback_ready_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_feedback_ready_rsp_data_t) == 28);


//
// TOUCH_SENSOR_CLEAR_MEM_WINDOW_RSP code is sent in response to TOUCH_SENSOR_CLEAR_MEM_WINDOW_CMD. This code will be followed
// by TOUCH_SENSOR_CLEAR_MEM_WINDOW_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:                   Command was processed successfully and memory window was set.
//      TOUCH_STATUS_CMD_SIZE_ERROR:            Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_PARAMS:            Input parameters are out of range.
//      TOUCH_STATUS_QUIESCE_IO_IN_PROGRESS:    Indicates that Quiesce I/O is already in progress and this command cannot be accepted at this time.
//
typedef struct touch_sensor_clear_mem_window_rsp_data
{
    u32          reserved[3];    // For future expansion
} touch_sensor_clear_mem_window_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_clear_mem_window_rsp_data_t) == 12);


//
// TOUCH_SENSOR_NOTIFY_DEV_READY_RSP code is sent in response to TOUCH_SENSOR_NOTIFY_DEV_READY_CMD. This code will be followed
// by TOUCH_SENSOR_NOTIFY_DEV_READY_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:               Command was processed successfully and sensor has been detected by ME FW.
//      TOUCH_STATUS_CMD_SIZE_ERROR:        Command sent did not match expected size.
//      TOUCH_STATUS_REQUEST_OUTSTANDING:   Previous request is still outstanding, ME FW cannot handle another request for the same command.
//      TOUCH_STATUS_TIMEOUT:               Sensor did not generate a reset interrupt in the time allotted. Could indicate sensor is not connected or malfunctioning.
//      TOUCH_STATUS_SENSOR_FAIL_FATAL:     Sensor indicated a fatal error, further operation is not possible. Error details can be found in ErrReg.
//      TOUCH_STATUS_SENSOR_FAIL_NONFATAL:  Sensor indicated a non-fatal error. Error should be logged by caller and init flow can continue. Error details can be found in ErrReg.
//
typedef struct touch_sensor_notify_dev_ready_rsp_data
{
    touch_err_reg_t   err_reg;         // Value of sensor Error Register, field is only valid for Status == TOUCH_STATUS_SENSOR_FAIL_FATAL or TOUCH_STATUS_SENSOR_FAIL_NONFATAL
    u32          reserved[2];    // For future expansion
} touch_sensor_notify_dev_ready_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_notify_dev_ready_rsp_data_t) == 12);


//
// TOUCH_SENSOR_SET_POLICIES_RSP code is sent in response to TOUCH_SENSOR_SET_POLICIES_CMD. This code will be followed
// by TOUCH_SENSOR_SET_POLICIES_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:           Command was processed successfully and new policies were set.
//      TOUCH_STATUS_CMD_SIZE_ERROR:    Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_PARAMS:    Input parameters are out of range.
//
typedef struct touch_sensor_set_policies_rsp_data
{
    u32          reserved[3];    // For future expansion
} touch_sensor_set_policies_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_set_policies_rsp_data_t) == 12);


//
// TOUCH_SENSOR_GET_POLICIES_RSP code is sent in response to TOUCH_SENSOR_GET_POLICIES_CMD. This code will be followed
// by TOUCH_SENSOR_GET_POLICIES_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:           Command was processed successfully and new policies were set.
//      TOUCH_STATUS_CMD_SIZE_ERROR:    Command sent did not match expected size. Other fields will not contain valid data.
//
typedef struct touch_sensor_get_policies_rsp_data
{
    touch_policy_data_t   policy_data;         // Contains the current policy
} touch_sensor_get_policies_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_get_policies_rsp_data_t) == 16);


//
// TOUCH_SENSOR_RESET_RSP code is sent in response to TOUCH_SENSOR_RESET_CMD. This code will be followed
// by TOUCH_SENSOR_RESET_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:                   Command was processed successfully and sensor reset was completed.
//      TOUCH_STATUS_CMD_SIZE_ERROR:            Command sent did not match expected size. Other fields will not contain valid data.
//      TOUCH_STATUS_INVALID_PARAMS:            Input parameters are out of range.
//      TOUCH_STATUS_TIMEOUT:                   Sensor did not generate a reset interrupt in the time allotted. Could indicate sensor is not connected or malfunctioning.
//      TOUCH_STATUS_RESET_FAILED:              Sensor generated an invalid or unexpected interrupt.
//      TOUCH_STATUS_QUIESCE_IO_IN_PROGRESS:    Indicates that Quiesce I/O is already in progress and this command cannot be accepted at this time.
//
typedef struct touch_sensor_reset_rsp_data
{
    u32          reserved[3];    // For future expansion
} touch_sensor_reset_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_reset_rsp_data_t) == 12);


//
// TOUCH_SENSOR_READ_ALL_REGS_RSP code is sent in response to TOUCH_SENSOR_READ_ALL_REGS_CMD. This code will be followed
// by TOUCH_SENSOR_READ_ALL_REGS_RSP_DATA.
//
// Possible Status values:
//      TOUCH_STATUS_SUCCESS:           Command was processed successfully and new policies were set.
//      TOUCH_STATUS_CMD_SIZE_ERROR:    Command sent did not match expected size. Other fields will not contain valid data.
//
typedef struct touch_sensor_read_all_regs_rsp_data
{
    touch_reg_block_t sensor_regs; // Returns first 64 bytes of register space used for normal touch operation. Does not include test mode register.
    u32          reserved[4];
} touch_sensor_read_all_regs_rsp_data_t;
C_ASSERT(sizeof(touch_sensor_read_all_regs_rsp_data_t) == 80);

//
// ME to Host Message
//
typedef struct touch_sensor_msg_m2h
{
    u32  command_code;
    touch_status_t	status;
    union
    {
        touch_sensor_get_device_info_rsp_data_t     device_info_rsp_data;
        touch_sensor_set_mode_rsp_data_t            set_mode_rsp_data;
        touch_sensor_set_mem_window_rsp_data_t      set_mem_window_rsp_data;
        touch_sensor_quiesce_io_rsp_data_t          quiesce_io_rsp_data;
        touch_sensor_hid_ready_for_data_rsp_data_t  hid_ready_for_data_rsp_data;
        touch_sensor_feedback_ready_rsp_data_t      feedback_ready_rsp_data;
        touch_sensor_clear_mem_window_rsp_data_t    clear_mem_window_rsp_data;
        touch_sensor_notify_dev_ready_rsp_data_t    notify_dev_ready_rsp_data;
        touch_sensor_set_policies_rsp_data_t        set_policies_rsp_data;
        touch_sensor_get_policies_rsp_data_t        get_policies_rsp_data;
        touch_sensor_reset_rsp_data_t               reset_rsp_data;
	touch_sensor_read_all_regs_rsp_data_t       read_all_regs_rsp_data;
    } m2h_data;
} touch_sensor_msg_m2h_t;
C_ASSERT(sizeof(touch_sensor_msg_m2h_t) == 88);


#define TOUCH_MSG_SIZE_MAX_BYTES    (MAX(sizeof(touch_sensor_msg_m2h_t), sizeof(touch_sensor_msg_h2m_t)))

#pragma pack()

#endif // _IPTS_MEI_MSGS_H_
