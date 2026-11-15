# IPU3 Cameras on Surface Pro 5 / Pro 6 / Book 2

Use this when the front and rear cameras worked on one distro (for example
Linux Mint with the linux-surface kernel) and then stopped working after a
switch to another distro (for example an Arch-based one such as EndeavourOS),
or after a kernel or package update.

## Why the cameras stop working

On these devices a camera is a stack of parts, not a single driver:

| Layer | What | Front (OV5693) / Rear (OV8865) / IR (OV7251) |
|---|---|---|
| Kernel | `ipu3-cio2`, `ipu3-imgu`, `ipu-bridge` | CSI-2 receiver and image processor |
| Kernel | `int3472` + `tps68470-*` | PMIC that powers and clocks the sensors |
| Kernel | `ov5693`, `ov8865`, `ov7251` | sensor drivers |
| Kernel | linux-surface `cameras` patches | ACPI probe ordering, IOMMU passthrough for the IPU, TPS68470 I2C daisy chain |
| Firmware | `intel/irci_irci_ecr-master_20161208_0213_20170112_1500.bin` | ImgU firmware |
| Userspace | libcamera (ipu3 pipeline + IPA) | turns raw Bayer data into a picture |
| Userspace | PipeWire libcamera plugin + WirePlumber | lets apps use the camera |

Mint with the linux-surface kernel has all of these. A typical Arch-based
install runs the stock `linux` kernel and does not install the PipeWire
libcamera plugin, so two layers are missing:

1. **Kernel patches.** Two of the required patches change code that is built
   into the kernel itself (`drivers/acpi/scan.c` and `drivers/iommu/intel/iommu.c`),
   not a loadable module. Without them, the sensor drivers probe before
   their PMIC is ready, and the IOMMU blocks the IPU's DMA. **No add-on module
   or DKMS package can fix this.** The fix is to run the linux-surface kernel,
   which already has these patches (see `patches/*/0013-cameras.patch`).
2. **Userspace.** Without `pipewire-libcamera`, apps don't see any
   libcamera camera. They see only the raw `ipu3-cio2` V4L2 nodes, which
   show a black or green image.

## What's here

| File | Install to | Purpose |
|---|---|---|
| `surface-camera-setup.sh` | run in place | Checks every layer above and reports what's missing. `--fix` installs packages and config |
| `modprobe.d/surface-ipu3-cameras.conf` | `/etc/modprobe.d/` | Soft dependencies so that the PMIC driver loads before the sensor drivers |
| `wireplumber/51-surface-ipu3-cameras.conf` | `~/.config/wireplumber/wireplumber.conf.d/` | Hides the raw `ipu3-*` V4L2 nodes and names the cameras "Surface Front Camera" / "Surface Rear Camera" (WirePlumber 0.5+) |

## Steps

```sh
# 1. See what's wrong (changes nothing)
./surface-camera-setup.sh

# 2. If it reports "not a linux-surface kernel", install that kernel first.
#    On Arch-based distros the script prints the exact commands. Reboot into it.

# 3. Install libcamera, the PipeWire plugin and the config files
sudo ./surface-camera-setup.sh --fix
systemctl --user restart wireplumber pipewire

# 4. Test
cam -l
```

`cam -l` should list the front and rear cameras (and the IR camera, if
`ov7251` is available). After that, GNOME Snapshot, Cheese, OBS and browsers
(through PipeWire) can use both cameras. For Firefox, set
`media.webrtc.camera.allow-pipewire` to `true`. For Chromium, enable
`chrome://flags/#enable-webrtc-pipewire-camera`.

If you must stay on a stock kernel, add `iommu=pt` to the kernel command
line. This restores the IPU's DMA, and the modprobe soft dependencies make
the PMIC-before-sensor ordering likely, but not certain. If a camera is still
missing, check `sudo dmesg | grep -iE 'ov5693|ov8865|int3472|tps68470'`.
The linux-surface kernel is still the reliable fix.

On WirePlumber 0.4 (older distros), the `.conf` rule file is ignored. You
still get the cameras, just without the renaming and hiding.
