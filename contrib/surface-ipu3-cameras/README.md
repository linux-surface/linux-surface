# Front, Rear and IR Cameras on Surface Pro 4/5/6 and Surface Book 1/2

These devices have three cameras on an Intel IPU3:

| Camera | Sensor | ACPI ID | Kernel driver |
|---|---|---|---|
| Front | OV5693 | `INT33BE` | `ov5693` |
| Rear | OV8865 | `INT347A` | `ov8865` |
| IR (Windows Hello) | OV7251 | `INT347E` | `ov7251` |

The sensors get their power, clock and (for IR) flood LED from an INT3472
"discrete" ACPI device (`intel_skl_int3472_discrete`). Their data goes
through `ipu3-cio2` (CSI-2 receiver). Normally libcamera then processes the
images with the IPU3 ImgU.

## What's here

| Path | Purpose |
|---|---|
| `surface-camd/` | **Userspace camera driver.** Takes raw data from the sensors and does its own image processing (auto exposure, white balance, demosaic). Publishes `/dev/video50` (front), `/dev/video51` (rear) and `/dev/video52` (IR) as normal webcams. Does not need libcamera, and **makes the IR camera work**, which libcamera's IPU3 pipeline cannot. See [surface-camd/README.md](surface-camd/README.md) |
| `surface-camera-setup.sh` | Checks every layer and reports what's broken. `--fix` installs and starts everything. `--report` collects all camera logs into one file |
| `modprobe.d/surface-ipu3-cameras.conf` | Makes sure the INT3472 driver loads before the sensor drivers |
| `wireplumber/51-surface-ipu3-cameras.conf` | Hides the raw `ipu3-cio2` nodes from apps (they show only black or green) and names the libcamera cameras |
| `wireplumber/52-surface-camd-only.conf` | Optional: hide the libcamera cameras when surface-camd provides them |

## Steps

```sh
./surface-camera-setup.sh               # 1. see what's wrong (changes nothing)
sudo ./surface-camera-setup.sh --fix    # 2. install surface-camd + config, start it
ffplay /dev/video50                     # 3. front; video51 rear; video52 IR
```

After that, browsers, video-call apps, OBS and Cheese list "Surface Front
Camera", "Surface Rear Camera" and "Surface IR Camera".

For Windows Hello-style face unlock, install Howdy and set
`device_path = /dev/video52` in its config.

## When a sensor doesn't appear at all

surface-camd needs the kernel's sensor drivers to probe. The setup script's
"Sensors" section shows, for each camera, whether:

- the ACPI device exists;
- an I2C device was created for it;
- a driver is bound to it.

If a sensor fails there, the failure is in the kernel. Run:

```sh
sudo ./surface-camera-setup.sh --report
```

Then attach `~/surface-camera-report.txt` to an issue. It contains the
following, which is what's needed to write a targeted kernel fix:

- the kernel log lines for the camera drivers;
- the INT3472 and ACPI state;
- the media graph;
- the package versions.

## Notes

- The IR camera needs IR light. surface-camd switches on the IR flood LED
  if the INT3472 driver exposes it (`/sys/class/leds/*ir_flood*`).
- If you must stay on a stock kernel, add `iommu=pt` to the kernel command
  line. Without the linux-surface IPU passthrough patch, the IOMMU blocks the
  IPU3's DMA.
