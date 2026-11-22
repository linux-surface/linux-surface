# Surface Laptop 1 infrared camera on Ubuntu 24.04

This records the built-in OV7251 infrared camera and synchronized emitter working on one original Microsoft Surface Laptop. The test system used Ubuntu 24.04, GNOME Shell 46 on X11, and the `7.3.3-1-surface` kernel.

This Ubuntu setup was developed and tested independently of [PR #2405](https://github.com/linux-surface/linux-surface/pull/2405). Both efforts build on Antoine Pichot's OV7251 strobe work. #2405 reports IR and PipeWire testing on Fedora 45 with a model-scoped 7.3.4 driver backport. This Ubuntu test used the [OV7251 backport from surface-ir-camera at d21b843](https://github.com/antoine79/surface-ir-camera/blob/d21b84370ebbc0019bcf7f35ebda439039721489/kernel/backport/ov7251.c), built against the installed Surface kernel headers. The loaded module reported source version `4461AC4BE54DB0FEC467D42`. The test machine also had OV9734 and IPU bridge overrides for its separate RGB camera. A configuration without those overrides was outside this test.

## Inspect the device

With `v4l-utils` installed, check the running kernel, loaded module, and available camera nodes.

```sh
uname -r
cat /sys/module/ov7251/srcversion
v4l2-ctl --list-devices
```

The test camera appeared under Intel IPU3 CIO2. On this machine the media graph was `/dev/media1` and the persistent IR capture path was `/dev/v4l/by-path/pci-0000:00:14.3-video-index2`. Use `media-ctl -d /dev/media1 -p` with the media device from your own system to verify the link from OV7251 to CIO2, then inspect the sensor subdevice controls and capture formats.

```sh
v4l2-ctl -d /dev/v4l/by-path/pci-0000:00:14.3-video-index2 --list-formats-ext
# Replace the subdevice below with the OV7251 node reported by media-ctl.
v4l2-ctl -d /dev/v4l-subdev6 --get-ctrl=strobe_output_enable
```

The receiver captured 640 × 480 packed 10-bit monochrome frames (`ip3y`) with an 832-byte row stride. The strobe control was off when idle.

## Results

| Test | Observation |
| --- | --- |
| Synchronized emitter | In a fixed-exposure and fixed-gain test, median alternating frame means were 21.1 and 144.3 on a 10-bit scale with strobe on. After strobe was turned off, both phases were 21.3. |
| GNOME lock-screen face authentication | A local development build of Howdy 3.0.2 used the CIO2 stream directly and unlocked GNOME. Covering the camera led to password fallback. |
| First face unlock after s2idle | Passed on 2026-11-22. Earlier attempts sometimes failed the whole-frame darkness filter and fell back to a password. |
| Capture comparison before and after resume | Both runs showed synchronized illumination in the first sampled pair. In one comparison, the selected frames fell above Howdy's configured 60% darkness threshold both before sleep and after resume. |

This result applies to the listed local build. See the [OV7251 strobe discussion](https://github.com/linux-surface/linux-surface/issues/739) and its [linux-media v3 series](https://lore.kernel.org/linux-media/20261113024059.1087031-1-antoine.pichot@gmail.com/) for upstream driver status.
