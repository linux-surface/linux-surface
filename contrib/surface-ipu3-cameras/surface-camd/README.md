# surface-camd: userspace camera driver for IPU3 Surface devices

`surface-camd` drives the front (OV5693), rear (OV8865) and IR (OV7251)
cameras on Surface Pro 4/5/6 and Surface Book 1/2 without libcamera. It
publishes each camera as an ordinary webcam:

| Device | Camera | Format |
|---|---|---|
| `/dev/video50` | Surface Front Camera | YUYV, 1296x972 |
| `/dev/video51` | Surface Rear Camera | YUYV, 1632x1224 |
| `/dev/video52` | Surface IR Camera | GREY, 640x480 |

Any app that uses webcams can open these devices: Firefox, Chrome, Zoom,
Teams, OBS, Cheese, ffplay, and Howdy (face unlock, IR camera).

## How it works

```
sensor (kernel driver) -> ipu3-csi2 -> ipu3-cio2 video node  (raw 10-bit, IPU3-packed)
        |
   surface-camd: unpack -> black level -> demosaic -> white balance -> gamma -> YUYV
        |            auto exposure writes the sensor's exposure/gain controls
        v
   v4l2loopback /dev/video5x  ->  applications
```

- It configures the media graph itself (enables the sensor -> CSI-2 link and
  sets the formats). This is the same work libcamera's IPU3 pipeline does,
  minus the ImgU.
- It processes the image in software. The colour cameras use a 2x2 demosaic,
  which halves the resolution and avoids colour fringes and zipper
  artifacts. Then grey-world white balance, sRGB gamma, and BT.601 YUYV.
- Auto exposure raises the sensor's exposure first, then its analogue gain,
  then a software gain of up to 4x.
- It reads the sensor's rotation (from ACPI through ipu-bridge) and flips the
  picture when the sensor is mounted upside down.
- **The IR camera** sends 10-bit monochrome data, which the IPU3 ImgU (and
  therefore libcamera) cannot process. surface-camd handles it directly. It
  turns on the IR flood LED (`/sys/class/leds/*ir_flood*`, provided by the
  INT3472 driver) while the IR camera streams.
- **On-demand streaming:** the sensor only runs while an application has the
  webcam open. This uses v4l2loopback's client-usage events, which need
  v4l2loopback 0.12.6 or newer. With older versions, it streams all the time.

## Install

The simplest way is `sudo ../surface-camera-setup.sh --fix`, which installs
v4l2loopback, builds, tests and installs this driver, and starts the services.
To do it by hand:

```sh
# needs: gcc, make, linux headers for your running kernel, v4l2loopback(-dkms)
make && make test
sudo make install
sudo modprobe -r v4l2loopback; sudo modprobe v4l2loopback
sudo systemctl daemon-reload
sudo systemctl enable --now surface-camd@front surface-camd@rear surface-camd@ir
ffplay /dev/video50
```

## Options

Set options in `/etc/default/surface-camd`, for example
`SURFACE_CAMD_ARGS="--saturation 1.1 -v"`, then run
`sudo systemctl restart surface-camd@front`.

| Option | Default | Meaning |
|---|---|---|
| `--size WxH` | 2592x1944 / 3264x2448 | Sensor mode. Use a smaller mode (e.g. `--size 1632x1224` on the rear camera) to lower CPU use |
| `--target F` | 0.18 | Auto-exposure brightness target |
| `--saturation F` | 1.25 | Colour saturation |
| `--black-level N` | 64 | Sensor black level (10-bit) |
| `--no-ir-led` | | Don't switch the IR LED |
| `--always-on` | | Stream even when no app is reading |
| `-v` | | Log auto-exposure and white-balance decisions |

## Using it alongside libcamera

Only one program can stream a sensor at a time. If libcamera works for you
and you want to keep using it, enable only the IR instance
(`surface-camd@ir`). To use surface-camd for all cameras, install
`../wireplumber/52-surface-camd-only.conf`. This stops PipeWire from also
offering the libcamera versions of the cameras.

## Troubleshooting

`journalctl -u surface-camd@front` explains failures:

- *"Sensor 'ov5693' is not registered with the IPU3"* means the kernel
  driver didn't probe. This is a kernel-level problem. Run
  `sudo ../surface-camera-setup.sh --report` and share the report.
- *"VIDIOC_REQBUFS failed: Device or resource busy"* when you run
  `surface-camd` by hand means the service already has that camera open.
  Stop it first (`sudo systemctl stop surface-camd@front`), or test through
  the service with `ffplay /dev/video50`.
- *"VIDIOC_STREAMON failed"* is followed by the formats along the pipeline
  and a hint to check `sudo dmesg`. surface-camd retries every 3 s while an
  app is waiting.
- *"Enabling link ... failed: Device or resource busy"* means another
  program (usually libcamera through PipeWire) is using the camera. See the
  previous section.
- *"Cannot open output /dev/video50"* means v4l2loopback isn't loaded with
  the config from `v4l2loopback-surface.conf`. Another tool's
  `options v4l2loopback` file can override it; `../surface-camera-setup.sh`
  lists them. Reload with
  `sudo modprobe -r v4l2loopback && sudo modprobe v4l2loopback`.
- The camera is only reserved while an app is watching. Logs show
  "streaming started", "first frame received" and "streaming stopped (N frames)".

## Tests

`make test` checks the IPU3 10-bit unpacking and the image processing on
synthetic frames:

- unpacking: bit layout, the kernel's line stride, and round trips;
- all four Bayer orders: a red patch must come out red;
- white balance: a grey scene with a colour cast must end up neutral;
- 180° rotation;
- the software-gain fallback for dark scenes;
- the IR (Y10) path.
