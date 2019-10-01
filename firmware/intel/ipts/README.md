# IPTS driver firmware
This is the driver firmware required for the `intel_ipts` module. It was
extracted from the `SurfaceBook2_Win10_18362_1904009_0` firmware bundle for
Windows.

### Filenames
* `SurfaceTouchServicingKernel*.bin` -> `vendor_kernel.bin`
* `SurfaceTouchServicingSFTConfig*.bin` -> `config.bin`
* `SurfaceTouchServicingDescriptor*.bin` -> `vendor_desc.bin`
* `iaPreciseTouchDescriptor.bin` -> `intel_desc.bin` (the same for all models)
* `ipts_fw_config.bin` (custom, the same for all models; https://github.com/ipts-linux-org/ipts-linux-new/commit/63a8944729d989dd98b0fa382fd9693d6a8c097f)

### HID descriptor
The HID descriptor for IPTS devices consists of `intel_desc.bin` and
`vendor_desc.bin` concatinated together by the driver. The result can be
decompiled by various tools found on the internet:
```bash
$ cat intel_desc.bin vendor_desc.bin > hid_desc.bin
```

### Quirks
At the moment, `MSHW0079` is using the HID descriptor from `MSHW0137`. It's own
HID descriptor is bugged and needs kernel mitigations to have the kernel properly
register touch input events. See: https://github.com/jakeday/linux-surface/issues/574
