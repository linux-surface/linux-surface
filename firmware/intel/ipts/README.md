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
The touchscreen contact count descriptor is incorrectly declared as being reported four times on the Surface Laptops (both 1 and 2, `vendor_desc.bin` in `MSHW0079`).
It is actually reported one time followed by three padding bytes.
The descriptor has been patched accordingly.

For more details, see https://github.com/qzed/linux-surface-kernel/wiki/IPTS-Firmware#bogus-hid-descriptor-for-surface-laptops-mshw0079.
