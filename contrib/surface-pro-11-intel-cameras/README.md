# Surface Pro 11 (Intel) — libcamera tuning for the three cameras

The Surface Pro 11 with Intel (Lunar Lake) has three cameras: a 13 MP rear
`ov13858`, a 5 MP front `imx681`, and an infrared `vd55g0` used for face login.
Once the kernel side is in place (see below) they enumerate and stream, and the
picture is unusable — wrong colour, wrong exposure, or both. What is missing is
tuning data, which is per-sensor and per-board and cannot come from upstream.

These three files are that data, measured on one machine. They are small: a
measured black level each, and deliberate omissions explained below. **Treat
them as a starting point**: they encode this unit's sensor and lens, and yours
may differ enough to want re-measuring.

## Where the files go

    /usr/share/libcamera/ipa/simple/ov13858.yaml
    /usr/share/libcamera/ipa/simple/imx681.yaml
    /usr/share/libcamera/ipa/simple/vd55g0.yaml

The directory is where your distribution installs libcamera's soft-ISP tuning;
on Ubuntu 26.04 that is the path above. libcamera picks the file by sensor name,
so no configuration is needed beyond putting them there.

## What you also need, and do not have yet

These files alone are not enough. As of 2026-10-08:

- **No released kernel has the camera side yet.** The front and infrared
  sensors have no driver in any release: the front one is on linux-media, the
  infrared one in review with ST. The rear `ov13858` driver is in-tree but
  assumes it is already powered at probe, which is false here, where an INT3472
  companion registers regulators, a clock and a reset GPIO for it; without the
  fix, `failed to find sensor: -5`. That fix is on linux-media, not merged, and
  the rear camera needs a few more patches that are accepted but not in a
  release. The current list, with links:
  https://github.com/linux-surface/linux-surface/discussions/2268

- **The front camera also needs a libcamera patch.** `imx681` has no
  `CameraSensorHelper`, so libcamera cannot convert its gain code and the picture
  comes out about 4.5x under-exposed. One small class, sent to libcamera-devel on
  2026-08-31 and held until the kernel driver lands, since a helper should not
  come ahead of its driver. Until then, `imx681.yaml` will not help on its own.

- **The infrared picture needs an IPU7 fix.** The staging IPU7 driver writes
  capture buffers without snooping the CPU's caches, so a program that reads a
  frame can get rows of an older one, visible wherever the scene moves. The
  one-line fix is on linux-media, not merged:
  https://lore.kernel.org/linux-media/20261007183311.24637-1-lsa.uz@pm.me/

Check whether these have landed before assuming the files are at fault.

## Things that will look like bugs and are not

**Neither colour file has a `Ccm` block.** That is deliberate. The matrices that
worked here are Intel's, read out of the Windows driver's tuning data, and these
files carry only what was measured on this machine. For `imx681` there is a
second reason: with a colour matrix this sensor turns clipped highlights
magenta; without one the colour is slightly flat and always sane. If you add a
matrix, check a scene with a bright window in it before deciding you have
improved anything.

**`vd55g0.yaml` has no `Agc` block.** Also deliberate: the infrared camera is
paired with an illuminator, and letting the AGC hunt makes face recognition less
reliable, not more. With a driver that reports the sensor as monochrome, as the
one going upstream does, libcamera 0.7 passes its frames through without
processing them, and nothing in this file applies.

**These files are written for libcamera 0.7.0.** The soft ISP changes
underneath: a colour matrix that gave a neutral-grey channel spread of 1.06 on
0.7.0 gave 13.9 on master. If you move to a newer libcamera, re-measure rather
than carrying these across.

## Face login

`howdy/libcamera_reader.py` is a Howdy recorder plugin that reads the infrared
camera through libcamera's Python bindings, since Howdy expects a V4L2 device
and the IR camera is only usable through libcamera here. Point Howdy's
`device_path` at it per its own documentation.

Three things in it are load-bearing and easy to lose when adapting it:

- It must check the format libcamera kept. `validate()` silently swaps a format
  the camera does not offer, and libcamera 0.7 offers a monochrome sensor only
  its raw R8, R10 and R10_CSI2P: no software ISP handles mono. Read as
  XRGB8888, those gave Howdy a frame a quarter of the true width, in which it
  found no face. With the driver going upstream, run on a 7.3-rc1 kernel, the
  reader as it is now found a face in every frame at 644x604.
- `get()` must return the real frame dimensions. Returning zeros makes Howdy's
  comparison step request an enormous resize — it asked for 118 GB here before
  it was fixed.
- `release()` must be guarded against being called twice. libcamera aborts
  inside `PipelineHandler::stop()` on the second call, taking the process with
  it.

## Provenance

Measured on a Surface Pro for Business 11th Edition with Intel, SKU
`Surface_Pro_11th_Edition_With_Intel_For_Business_2103`, Core Ultra 7 268V,
Ubuntu 26.04, kernel 7.0.0-30, libcamera 0.7.0. The black levels are measured
on this machine's sensors. Nothing in these files comes from the Windows
driver.
