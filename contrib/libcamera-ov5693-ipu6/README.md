# libcamera tuning for the OV5693 front camera (IPU6)

Software-ISP tuning file for the OV5693 front camera on IPU6 Surface devices, for use once the camera produces frames.
Without a tuning file libcamera's software ISP uses `uncalibrated.yaml`, which leaves a green cast under
fluorescent/LED office light.

Files:

- `ov5693.yaml`: the tuning file.
- `measure-raw.py`: measures black level and white-balance gains from raw frames, so you can re-derive the numbers for your unit.

## Requirements

- **libcamera 0.7.2 or newer.** 0.7.2 fixed highlight-saturation handling in the software ISP and ships a black level for this
  sensor; the file was tuned against it. On 0.7.1 clipped highlights show a slight magenta tint, with or without this file.
- A kernel where the front camera streams on IPU6. This directory does not fix that: it needs the `MIPI_CTRL00` write from
  [#2171](https://github.com/linux-surface/linux-surface/pull/2171). The 2x2-binned mode
  ([#2252](https://github.com/linux-surface/linux-surface/pull/2252)) was **not** tested with this tuning.

## Install

```
mkdir -p ~/.config/libcamera/ipa/simple
cp ov5693.yaml ~/.config/libcamera/ipa/simple/
```

For all users, use `/usr/share/libcamera/ipa/simple/` instead. The file is named after the sensor, so no further configuration is needed.
Check it is picked up: libcamera logs `Using tuning file ...` (run `cam -c1 -C5` with `LIBCAMERA_LOG_LEVELS=IPAProxy:1`).
Delete the file to revert. The directory is named after the IPA module; if a newer libcamera renames the software IPA
(the development branch has moved its sources to `src/ipa/softisp`), use the directory named in that log line.

This is not a colour-chart calibration. Treat the values as a starting point.

## What it changes and why

The software ISP's `Awb` averages every pixel, clipped ones included. In an office with bright ceiling fixtures and a
window in view, 5 to 12 percent of raw quads clip, so grey-world under-corrects red and blue and the image keeps a green
cast. The matrix adds saturation 1.2 with red x1.12 and blue x1.10 on top of the AWB gains.

Same scene, same position, five captures (`gst-launch-1.0 libcamerasrc`, 1920x1080, last of 60 frames):

| libcamera | Tuning | Ceiling R,G,B | Ceiling G minus R | Highlight tint (magenta excess) |
|---|---|---|---|---|
| 0.7.1 | none | 146, 160, 150 | +14 | +2.3 |
| 0.7.1 | this file* | 167, 170, 171 | +2 | +2.9 |
| 0.7.2 | none | 163, 171, 167 | +8 | -3.0 (slightly green) |
| 0.7.2 | this file | 170, 172, 173 | +1 | 0.0 |

\* with an explicit black level of 1024, which 0.7.2 now supplies itself.

Ceiling is the mean of an area of white ceiling tiles. Highlight tint is `(R+B)/2 - G` over pixels with luma above 235
(light fixtures and window); 0 is neutral white. Brightness differs between 0.7.1 and 0.7.2 because 0.7.2 replaced the
AGC, so compare the colour columns, not the levels.

**Black level.** Dark raw frames (lens covered, AE settled, 10 frames) give a median of 16 on the 10-bit scale for G and B
(R reads 20, which may be light leaking through the cover). That is 1024 on libcamera's 16-bit scale and matches the value
0.7.2's sensor helper already uses, so the tuning file does not set one. Black level was not compared on the processed
output: the covered-lens test of the shipped configurations was not completed.

**What this cannot fix.** The software ISP ignores `ExposureValue`, manual exposure and gain, and `ColourGains` on this
sensor, so a bright window or ceiling light in view cannot be compensated in software. It also has no denoising.

## Optional: gamma and contrast

Applications that pass libcamera controls can lower gamma and raise contrast (`gamma=1.8 contrast=1.1` visibly improved depth
and skin tone). Through GStreamer:

```
gst-launch-1.0 libcamerasrc gamma=1.8 contrast=1.1 ! video/x-raw,width=1920,height=1080 ! videoconvert ! ...
```

This only affects that pipeline. Applications that reach the camera through PipeWire get the defaults, so put these
properties in the loopback recipe on the Camera Support wiki page if you want them in Zoom or Teams.

## Measuring your own unit

Capture raw frames alongside a processed stream, so AE and AWB keep running:

```
cam -c1 -C20 -s role=viewfinder,width=1920,height=1080 -s role=raw -Fframe#.bin
```

Each raw frame is about 10 MB and `-F` writes every frame, so keep `-C` small and delete the files afterwards
(on a tmpfs this fills the disk quickly).

1. Cover the lens fully for the dark capture, then `./measure-raw.py dark frame-stream1-*.bin` prints the black level.
2. Uncover, capture a normal scene, then `./measure-raw.py wb --black 16 frame-stream1-000019.bin`. Add `--roi x0,y0,x1,y1`
   (half-resolution coordinates) to measure a neutral region only, such as a ceiling tile or a white sheet.

Raw frames are 2592x1944 SBGGR10 in 16-bit containers. `--size WxH` selects another resolution; that is untested.

## Tested on

One Surface Pro 8 front camera, libcamera 0.7.1 (distribution package) and 0.7.2 (built from the release tag, simple pipeline,
software ISP), under mixed fluorescent/LED ceiling light and daylight. Measurements are from a single unit and scene, so
reports from other devices and lighting are welcome.
