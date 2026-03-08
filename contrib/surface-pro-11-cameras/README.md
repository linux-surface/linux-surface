# Surface Pro 11 (Intel) Camera Support

Automated setup for the IPU7 camera pipeline on the Surface Pro 11 Business
(Intel Lunar Lake). Builds and installs all userspace components needed to use
the front and rear cameras.

## What this provides

| Component | Source | Purpose |
|-----------|--------|---------|
| `ipu7-camera-bins` | Intel GitHub | IPU7 firmware (`ipu7x-fw.bin`) and proprietary ISP libraries |
| `ipu7-camera-hal` + patch | Intel GitHub + `ipu7-camera-hal.patch` | Camera HAL with Surface Pro 11 sensor configs |
| `icamerasrc` | Intel GitHub | GStreamer source plugin for camera capture |
| Graph settings binaries | Included in HAL patch (generated from IMX471 template) | ISP pipeline configuration for IMX681 and OV13858 |
| AIQB tuning files | Microsoft firmware update | ISP calibration data (auto-exposure, white balance, color, noise) |
| AIQD persistence | tmpfiles.d config | Caches ISP calibration across reboots |
| v4l2loopback bridge | modprobe.d + systemd service + udev rules | Virtual /dev/video devices for Firefox, Zoom, Chrome, etc. Hides raw ISYS nodes. |

### Cameras supported

| Camera | Sensor | Resolution | Output | Status |
|--------|--------|-----------|--------|--------|
| Front | Sony IMX681 | 3844x2640 RAW10 | 1920x1080 NV12 | Working |
| Rear | OmniVision OV13858 | 4224x3136 RAW10 | 1920x1080 NV12 | Working |
| IR | ST VD55G0 | 640x600 RAW8 | Raw only (v4l2) | ISYS only, no ISP |

### Features

- Hardware ISP pipeline: BLC, linearization, lens shading correction, defective pixel
  correction, Bayer noise reduction (BNLM), demosaic, white balance, color correction,
  global/local tone mapping, extended noise reduction (XNR5.2), gamma, color space
  conversion, temporal noise reduction (TNR7), contrast-adaptive sharpening
- Auto Exposure (AE) with convergence, manual exposure/gain, EV compensation
- Auto White Balance (AWB) with 12 preset modes + manual gains/CCT
- Antibanding (auto/50Hz/60Hz/off)
- ISP calibration persistence (AIQD) across sessions

## Prerequisites

1. **Kernel with Surface Pro 11 camera patches**. Patches `0018-surface-pro-11-intel-cameras.patch`
   and `0021-surface-pro-11-ir-camera.patch` from `linux-surface/patches/6.17/` must be applied.
   These add the IMX681 sensor driver, OV13858 fixes, VD55G0 IR driver, and IPU bridge entries.

2. **Kernel headers** for the running kernel (the install script builds the IPU7 out-of-tree
   kernel modules automatically):
   ```bash
   sudo apt install linux-headers-$(uname -r)   # Debian/Ubuntu
   sudo dnf install kernel-devel                 # Fedora
   ```

3. **Build dependencies:**
   ```bash
   # Debian/Ubuntu
   sudo apt install build-essential cmake autoconf automake libtool pkg-config git \
     libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
     libgstreamer-plugins-bad1.0-dev libva-dev \
     msitools  # for AIQB extraction from Microsoft firmware
   ```

## Installation

```bash
sudo ./install.sh
```

The script will (each step asks for confirmation):
1. Clone Intel's `ipu7-drivers`, build and install IPU7 out-of-tree kernel modules
2. Clone Intel's `ipu7-camera-bins` and install firmware + proprietary libraries
3. Clone Intel's `ipu7-camera-hal`, apply the Surface Pro 11 patch (which includes
   sensor configs and pre-built graph settings binaries), and build the HAL
4. Clone Intel's `icamerasrc` and build the GStreamer plugin
5. Set up v4l2loopback virtual cameras for application compatibility
6. Configure AIQD calibration persistence
7. Install AIQB tuning files (from Microsoft firmware or upstream fallback)

Build artifacts are placed in `/tmp/surface-camera-build/` by default. Use
`--build-dir=PATH` to change this.

### v4l2loopback virtual cameras (Step 5)

The IPU7 cameras are only accessible via GStreamer (`icamerasrc`), which means
standard V4L2 applications (Firefox, Zoom, Chrome, etc.) cannot see them. Step 5
sets up v4l2loopback to create virtual `/dev/video` devices that bridge the gap:

- `/dev/video50` — Surface Front Camera (IMX681)
- `/dev/video51` — Surface Rear Camera (OV13858)

A bridge script (`surface-camera-bridge.sh`) pipes `icamerasrc` output to the
virtual device. **The bridge must be running for apps to see the camera** —
v4l2loopback requires an active writer, so on-demand activation is not possible.

```bash
# Start/stop manually
systemctl --user start surface-camera-bridge@imx681-uf
systemctl --user stop surface-camera-bridge@imx681-uf

# Or auto-start on login (camera will always be active when logged in)
systemctl --user enable surface-camera-bridge@imx681-uf
```

Note: while the bridge is running, the camera privacy LED will be on and the
camera hardware is active. Stop the bridge to turn it off.

Requires `v4l2loopback-dkms` (Debian/Ubuntu) or `v4l2loopback` (Fedora). The
install script will offer to install it if missing.

#### ISYS device hiding

The IPU7 hardware exposes 32 raw ISYS capture nodes (`/dev/video0`–`/dev/video31`)
that are non-functional for normal applications. These confuse camera selection in
Firefox, Zoom, etc. Step 5 hides them using two mechanisms:

1. **udev rules** (`91-v4l2loopback-surface.rules`): Set ISYS devices to
   `root:root 0600` and strip user ACLs, so unprivileged users and sandboxed
   apps (snap Firefox) cannot open them.
2. **WirePlumber rule** (`50-disable-ipu7-isys.conf`): Disables ISYS nodes in
   PipeWire so they don't appear as Video/Source devices.

The camera HAL still needs ISYS access to program the hardware. The bridge script
calls a root helper (`surface-camera-isys-grant.sh`) via a passwordless sudoers
rule to temporarily re-grant the user's ACLs before starting the GStreamer pipeline.

#### Installed files

| File | Purpose |
|------|---------|
| `/etc/modprobe.d/v4l2loopback-surface.conf` | Module options (2 devices, video50/51, exclusive_caps) |
| `/etc/modules-load.d/v4l2loopback.conf` | Auto-load v4l2loopback at boot |
| `/usr/local/bin/surface-camera-bridge.sh` | Bridge script (icamerasrc → v4l2sink) |
| `/usr/local/bin/surface-camera-isys-grant.sh` | Root helper to re-grant ISYS ACLs |
| `/etc/sudoers.d/surface-camera-isys-grant` | Passwordless sudo for the grant helper |
| `/etc/systemd/user/surface-camera-bridge@.service` | Systemd user service template |
| `/etc/udev/rules.d/91-v4l2loopback-surface.rules` | ISYS hiding + loopback tagging |
| `~/.config/wireplumber/wireplumber.conf.d/50-disable-ipu7-isys.conf` | PipeWire ISYS hiding |

### AIQB tuning files (Step 7)

The AIQB files provide sensor-specific ISP calibration (color matrices, lens
shading, noise model, auto-exposure curves). For best image quality, extract
them from the Microsoft Surface Pro 11 (Intel) firmware update `.msi` file.

**To get the firmware:**
1. Go to https://www.microsoft.com/en-us/download/details.aspx?id=108013
2. Download the `.msi` file (filename: `SurfacePro11withIntel_Win11_*.msi`)
3. Either pass it on the command line or enter the path when prompted at Step 6

```bash
# Provide the firmware .msi on the command line
sudo ./install.sh --firmware=/path/to/SurfacePro11withIntel_*.msi

# Or run without --firmware and enter the path interactively at Step 6
sudo ./install.sh
```

Extraction requires `msiextract` (from `msitools`) or `7z` (from `p7zip-full`).

### Without AIQB extraction (upstream fallback)

If you don't have the firmware `.msi`, press Enter at the prompt (or use
`--skip-aiqb`) to install an upstream fallback AIQB (OV08X40) instead:

```bash
sudo ./install.sh --skip-aiqb
```

The fallback AIQB is already included in Intel's HAL repository (no download
needed). It provides a functional camera with auto-exposure, auto white balance,
and hardware noise reduction — but since the color matrices, lens shading
tables, and noise model are calibrated for a different sensor, you'll see:
- Slightly inaccurate colors / color cast
- Less effective noise reduction
- Non-optimal lens shading correction

You can upgrade to genuine AIQBs at any time by re-running with `--firmware`.

## Verification

```bash
# Check all components
sudo ./install.sh --status

# Reboot first, then test front camera
rm -f /dev/shm/sem.camlock
GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0 \
  gst-launch-1.0 icamerasrc device-name=imx681-uf num-buffers=30 \
  ! 'video/x-raw,format=NV12,width=1920,height=1080' \
  ! videoconvert ! autovideosink

# Test rear camera
GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0 \
  gst-launch-1.0 icamerasrc device-name=ov13858-uf num-buffers=30 \
  ! 'video/x-raw,format=NV12,width=1920,height=1080' \
  ! videoconvert ! autovideosink
```

Note: the first ~15 frames will be very dark while auto-exposure converges.
Use `num-buffers=35` and check the later frames.

## Uninstallation

```bash
sudo ./install.sh --remove
```

This removes the HAL library, GStreamer plugin, Surface-specific config files,
AIQD persistence config, and v4l2loopback configuration. Intel's proprietary
libraries and firmware are left in place (they don't conflict with anything).

## What the HAL patch changes

The `ipu7-camera-hal.patch` applies the following changes to Intel's upstream
`ipu7-camera-hal` repository:

### New files
- `sensors/imx681-uf.json` — IMX681 front camera sensor configuration
- `sensors/ov13858-uf.json` — OV13858 rear camera sensor configuration
- `libcamhal_configs.json` — adds `imx681-uf-2` and `ov13858-uf-0` camera entries

### Source code changes

| File | Change | Why |
|------|--------|-----|
| `GraphUtils.cpp` | Remove `bpp == 8` guard on LBFF output + BBPS input terminals; add all BBPS TNR terminals to V420 block | 8-bit template produces wrong FOURCC (GREY instead of V420), TNR terminals get "no fourcc" errors |
| `AiqUnit.cpp` | Add `CCA_MODULE_LTM`, `OB`, `BCOM`, `DSD` to 3A bitmap | Enable local tone mapping and optical black/defect correction modules |
| `Intel3AParameter.cpp` | Cap ISO at 4x base; copy frame_time_max to exposure_time_max if unset | Force CCA to prefer longer exposure over high gain; prevent AIQB default from limiting exposure |
| `AiqSetting.cpp` | Change AE FPS range from {10, 60} to {5, 30} | Allow longer exposure in low light; match actual sensor capability |
| `CameraContext.h` | Change default NR from LEVEL_2 to LEVEL_1 (strength 20) | Maximum hardware noise reduction |
| `ParameterConvert.cpp` | Only constrain max FPS from requested framerate, keep min low | Allow CCA to extend frame time for low-light |
| `Utils.cpp` | Add SGRBG10, SRGGB10, SGBRG10, SBGGR10 FOURCC cases | Support all Bayer format variants in BPL calculation |
| `AiqInitData.cpp` | Auto-create parent directory for AIQD save path | AIQD save fails silently if `/run/camera/` doesn't exist |
| `AiqCore.cpp` | Add periodic AE result logging | Diagnostic: log exposure/gain/ISO every 30 frames |
| `IpuPacAdaptor.cpp` | Add kernel group debug logging; better error reporting | Diagnostic: log PAC configuration details on failure |

### Sensor JSON configuration

Both sensor JSONs define:
- Sensor resolution, Bayer format (SGRBG10), media control links
- Output resolutions: 1920x1080, 1280x720, 640x480, 640x360
- Exposure range: 100–200,000 µs (extended from default 100,000)
- AWB modes: 12 presets including manual CCT, white point, and gain
- Graph settings binary and AIQB tuning file references

## Graph settings binaries

The ISP pipeline configuration binaries (`IMX681_MSHW0580.IPU7X.bin` and
`OV13858_MSHW0580.IPU7X.bin`) are included in the HAL patch. They were
generated from the IMX471 template with sensor-specific dimensions
patched in.

The template determines the ISP bit depth — IMX471 uses 8-bit intermediate
format (NV12 output), which is the only working configuration. 10-bit templates
(e.g., OV13B10) produce P010 output that the HAL cannot handle.

## Known limitations

- **Output resolution**: Only 1920x1080 works. Other resolutions (720p, 480p)
  hang due to ISP-internal parameter mismatches in the graph settings.
- **Video stabilization**: Not available. Would require Graph 100003 (DVS)
  configuration, which has not been reverse-engineered.
- **Runtime ISP tuning**: The closed-source AIC library ignores runtime NR/EE/image
  parameters. All image quality tuning is baked into the AIQB file.
- **IR camera**: ISYS direct capture works (raw 640x600 8-bit), but the ISP
  pipeline (PSys) hangs on VD55G0 tasks. The IR camera is Windows Hello only.
- **Frame rate**: IMX681 runs at ~16fps at full resolution. OV13858 at ~13fps
  in low light (AE extends exposure).
- **Teardown crash**: OV13858 sometimes causes a "corrupted double-linked list"
  crash on pipeline teardown. Frame data is unaffected.

## Troubleshooting

### "Could not open camera HAL" or no video output
```bash
# Clean stale camera locks
rm -f /dev/shm/sem.camlock
ipcrm -m $(ipcs -m | grep 0x0043414d | awk '{print $2}') 2>/dev/null

# Check modules are loaded
lsmod | grep intel_ipu7

# Check dmesg for errors
dmesg | grep -i "ipu\|imx681\|ov13858"
```

### Dark/black frames
Auto-exposure needs ~15 frames to converge. Capture at least 30 frames and
check the later ones. In very low light, IMX681 converges at ~130ms exposure /
4x gain (~7.6 fps).

### Purple/blue color cast
The IMX681 AIQB expects GRBG Bayer order. If the kernel driver's H-flip
(register 0x0101) is not set, the native RGGB gets demosaiced as GRBG,
swapping red and blue channels.

### NEVER reload intel_ipu7_isys
Reloading the ISYS module at runtime freezes the Surface Pro 11 completely
(requires hardware power-off). Always reboot instead.

## Upstream status

| Component | Repository | Status |
|-----------|-----------|--------|
| IMX681 kernel driver | linux kernel | linux-surface patch only |
| VD55G0 kernel driver | linux kernel | linux-surface patch only |
| OV13858 fixes | linux kernel | linux-surface patch only |
| IPU bridge entries | linux kernel | linux-surface patch only |
| HAL Surface config | [ipu7-camera-hal](https://github.com/intel/ipu7-camera-hal) | Not yet upstreamed |
| Graph settings gen | This contrib | New tool |
| AIQB tuning files | Microsoft firmware | Extracted at install time (not redistributable) |
