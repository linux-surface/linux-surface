# SB3 sensors & peripherals (7.3.0-surface-ipu4+)

Checked live on Surface Book 3.

## Working

| Device | Status |
|--------|--------|
| Accel / gyro / rotation / gravity (ISH `8087:0AC2`) | OK — `iio-sensor-proxy` `HasAccelerometer=true` |
| Touch + stylus (IPTS) | OK — in-tree after rebuild, or OOT `tools/rebuild-ipts-module.sh` |
| Keyboard / touchpad (SSAM HID) | OK |
| Screen brightness (`intel_backlight`) | OK |
| Batteries BAT1/BAT2, DTX | OK |

## Ambient light

`HasAmbientLight=false`. `hid_sensor_als` is loaded but the ISH hub on this unit does **not** expose an illuminance IIO channel (only accel/gyro/rotation/gravity). Not a missing kernel module — sensor is absent from the HID report set. Auto-brightness from ALS will not work.

## Keyboard backlight (Plasma “nan%”)

Hardware backlight is driven by the Surface EC. Linux does **not** get a `kbd_backlight` LED sysfs node and cannot read/set a percentage (same as SL3 — linux-surface discussion #1193). The Fn key still toggles brightness in firmware.

Plasma showed **“Jasność nan%”** because UPower ≥1.91.4 exports an empty `KbdBacklight` with `GetMaxBrightness=0`, and PowerDevil 6.7.5 treated that as supported (`0/0` → NaN). Fixed upstream in PowerDevil 6.7.6 (BUG 525916). On this machine we rebuilt `powerdevil-6.7.5` with that one-line patch and reinstalled it — the bogus slider should disappear after PowerDevil restart (already done).

There is **no** way to show a real OS-controlled percentage until someone reverse-engineers an EC/SSAM backlight API (not available today).

## Xbox Wireless (built-in `045e:091e` XBOX ACC)

Kernel has no in-tree driver for the SB3 built-in adapter. Installed **xone** (DKMS, dlundqvist fork) + dongle firmware `xone_dongle_091e.bin`:

```bash
# already on this machine under /usr/local/src/xone
cd /usr/local/src/xone
sudo ./install.sh
sudo bash install/firmware.sh --skip-disclaimer   # needs bsdtar
sudo modprobe xone_dongle
```

`lsusb -t` should show `Driver=xone-dongle` on the XBOX ACC port. Pair the controller with the Xbox button (not Bluetooth). After a kernel update, rebuild: `sudo dkms install xone/unknown -k $(uname -r)` or re-run `install.sh`.
