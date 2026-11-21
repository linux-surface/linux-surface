# Surface Pro 10 Front Camera C-PHY Support

## Status
BLOCKED ON FIRMWARE - kernel driver is complete but Linux ipu6epmtl_fw.bin
does not support C-PHY stream processing for Meteor Lake.

## What works
- C-PHY detection and flag propagation
- CSI frontend registers correct (FE_MODE=1, PPI_CFG=0x9)  
- DWC PHY reaches IDLE state
- IMX681 sensor starts streaming
- Firmware accepts stream open (src=4)

## What is broken
Linux firmware never sends FRAME_SOF after stream start.
Windows firmware uses src=6 (CSI2_3PH_CPHY_PORT0) successfully.
Linux firmware rejects src=6 with FW_INTERNAL_CONSISTENCY error.

## Required fix
Intel must release updated ipu6epmtl_fw.bin with C-PHY support.
