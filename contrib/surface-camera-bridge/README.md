# surface-camera-bridge

A userspace v4l2 bridge that makes the **front camera** on the Microsoft
Surface Go 2 usable in Chrome, Zoom, cheese, OBS, browser webcam tests, and
any other application that expects a plain `/dev/videoN` device. Tested on
Ubuntu 22.04 with the `linux-surface` 6.18 kernel.

## The problem

The Surface Go 2 has two cameras (front: ov8865, rear: ov5693) plugged into
Intel's IPU3 CSI-2 pipeline via ACPI. The `linux-surface` kernel exposes the
full IPU3 stack — `ipu3-imgu`, `ipu3-cio2`, `ov5693`, `ov8865`, `dw9719` VCM
— but the IPU3 pipeline is *not* a v4l2 capture device. It's a staging
driver whose raw CSI-2 frames have to go through userspace processing
(debayer, demosaic, color correction, auto-exposure) via libcamera before
they're viewable. That means:

1. A plain `v4l2-ctl --stream-mmap --device=/dev/video1` produces garbage.
2. `libcamera` sees both cameras (`cam -l` lists them) but only exposes them
   through its own API. Chrome, Zoom, cheese, etc. don't speak libcamera.
3. The libcamera IPU3 IPA pipeline ships only a stub `uncalibrated.yaml` —
   no `ov8865.yaml` exists anywhere upstream (see [linux-surface#1569][1569]).
   Without per-sensor tuning data AND without a v4l2 bridge, the camera is
   effectively unusable for normal video-call applications.

[1569]: https://github.com/linux-surface/linux-surface/issues/1569

## The solution

Three cooperating systemd services plus a `v4l2loopback` virtual device:

```
                        systemctl start/stop
 ┌───────────────────┐  ┌─────────────────────────┐
 │ Chrome / Zoom /   │  │ surface-camera-watcher  │
 │ cheese / browser  │  │  polls /proc/*/fd for   │
 │                   │  │  external openers of    │
 │   opens & reads   │  │  /dev/video42           │
 │    /dev/video42   │  └────────────┬────────────┘
 └────────┬──────────┘               │ triggers
          │                          ▼
          │               ┌─────────────────────┐
          │               │ surface-camera-idle │
          │  v4l2loopback │  when no readers:   │
          ▼               │  writes 5fps black  │
   /dev/video42 ◄─────────┤  frames, no HW,     │
   (v4l2loopback)         │  camera LED off     │
          ▲               └──────────┬──────────┘
          │                          │ Conflicts=
          │               ┌──────────▼──────────┐
          │               │surface-camera-bridge│
          └───────────────┤ gstreamer+libcamera │
                          │ → ffmpeg + filter   │
                          │ → /dev/video42      │
                          │ camera LED ON       │
                          └─────────────────────┘
```

* **`surface-camera-idle.service`** writes a 5fps black YUV420 frame to
  `/dev/video42` at all times when nothing is actively using the camera.
  Its sole purpose is to keep `/dev/video42` streamable so a reader's
  `VIDIOC_STREAMON` never returns `EIO`. Zero hardware contact; camera LED
  stays **off**.
* **`surface-camera-bridge.service`** is the real capture pipeline:
  libcamera → gstreamer → raw YUV420 → ffmpeg (channel mixer + eq + gamma)
  → `/dev/video42`. It also sets `exposure` and `analogue_gain` directly on
  the `ov8865` v4l2-subdev because the uncalibrated libcamera IPA leaves
  them near minimum. While this runs the camera LED is **on**, which is
  correct consumer behavior.
* **`surface-camera-watcher.service`** is a small Python daemon that
  scans `/proc/*/fd` every 100ms for symlinks pointing to `/dev/video42`.
  When it sees an external opener (Chrome, Zoom, …) it starts the
  `surface-camera-bridge` service (which, via `Conflicts=`, automatically
  stops the idle service). When the last external opener has been gone
  for ≥3 seconds, it starts the idle service (which stops the bridge).

The idle → bridge → idle transitions are seamless from the reader's
perspective because both services write **the same format** (yuv420p at
1280×720). The reader's `VIDIOC_DQBUF` just blocks briefly during the swap
and then continues receiving frames.

Video apps see "Surface Front Cam" as a regular webcam; nothing knows
libcamera is involved.

## Repository contents

```
contrib/surface-camera-bridge/
 │
 ├─ README.md                               ← this file
 ├─ surface-camera-bridge                   ← capture pipeline script (bash)
 ├─ surface-camera-watcher                  ← watcher daemon (python3)
 │
 ├─ systemd/
 │   ├─ surface-camera-bridge.service
 │   ├─ surface-camera-idle.service
 │   └─ surface-camera-watcher.service
 │
 ├─ modprobe/
 │   ├─ modules-load.conf                   ← /etc/modules-load.d/v4l2loopback.conf
 │   └─ v4l2loopback.conf                   ← /etc/modprobe.d/v4l2loopback.conf
 │
 └─ ipa/
     ├─ ov5693.yaml                         ← IPU3 IPA tuning stub (rear)
     └─ ov8865.yaml                         ← IPU3 IPA tuning stub (front)
```

## Dependencies

* **linux-surface kernel** with the IPU3 drivers enabled
  (`ipu3-imgu`, `ipu3-cio2`, `ov5693`, `ov8865`, `dw9719`). All
  `linux-image-surface` packages since ~6.10 have these.
* **libcamera** with the IPU3 pipeline handler and GStreamer plugin built
  (`libgstlibcamera.so`). Ubuntu's `libcamera0` + `gstreamer1.0-libcamera`
  packages work; the source tarball from git.libcamera.org also works if
  you build it with `-Dpipelines=ipu3 -Dgstreamer=enabled`.
* **v4l2loopback ≥ 0.15** kernel module. The Ubuntu 22.04 packaged
  `v4l2loopback-dkms` (0.12.7) does not compile against kernels ≥ 6.6 —
  you need a newer build from https://github.com/umlaeute/v4l2loopback.
  The upstream source ships a working `dkms.conf`; install via:
  ```bash
  sudo cp -r v4l2loopback /usr/src/v4l2loopback-0.15.3
  sudo dkms add     -m v4l2loopback -v 0.15.3
  sudo dkms build   -m v4l2loopback -v 0.15.3
  sudo dkms install -m v4l2loopback -v 0.15.3
  ```
  With `AUTOINSTALL=yes` in `dkms.conf`, subsequent kernel upgrades rebuild
  the module automatically.
* **ffmpeg**, **gstreamer1.0-tools**, **v4l-utils**, **python3**.

## Installation

```bash
# 1. Install the IPA stubs (silences "Configuration file ov8865.yaml not found")
sudo install -m 644 ipa/ov5693.yaml /usr/share/libcamera/ipa/ipu3/ov5693.yaml
sudo install -m 644 ipa/ov8865.yaml /usr/share/libcamera/ipa/ipu3/ov8865.yaml

# 2. Install the bridge and watcher scripts
sudo install -m 755 surface-camera-bridge  /usr/local/bin/surface-camera-bridge
sudo install -m 755 surface-camera-watcher /usr/local/bin/surface-camera-watcher

# 3. Install module-load config so v4l2loopback comes up at boot
sudo install -m 644 modprobe/modules-load.conf /etc/modules-load.d/v4l2loopback.conf
sudo install -m 644 modprobe/v4l2loopback.conf /etc/modprobe.d/v4l2loopback.conf

# 4. Install the three systemd units
sudo install -m 644 systemd/surface-camera-idle.service    /etc/systemd/system/
sudo install -m 644 systemd/surface-camera-bridge.service  /etc/systemd/system/
sudo install -m 644 systemd/surface-camera-watcher.service /etc/systemd/system/

# 5. Load module and enable the always-on services
sudo modprobe v4l2loopback
sudo systemctl daemon-reload
sudo systemctl enable --now surface-camera-idle.service
sudo systemctl enable --now surface-camera-watcher.service
# Do NOT `enable` surface-camera-bridge — the watcher starts it on demand.
```

Verify:

```bash
cam -l                                  # should list the front camera
v4l2-ctl --list-devices                 # should show "Surface Front Cam"
systemctl status surface-camera-idle    # active (ffmpeg writing black)
systemctl status surface-camera-watcher # active (polling)
```

Then open Chrome, go to a webcam test page, pick "Surface Front Cam",
and you should see the camera feed inside ~1 second. The camera LED
turns on *only* while an application is actually using it.

## Tuning image quality

Image quality from libcamera's uncalibrated IPU3 IPA is not great: the
color correction matrix is identity, auto-exposure isn't tuned, and the
ov8865's Bayer pattern is green-heavy so frames come out with a strong
green cast at nearly-minimum exposure. The bridge script compensates in
two ways, both exposed as environment variables you can override via
`systemctl edit surface-camera-bridge`:

* **Hardware exposure/gain**: `SENSOR_EXPOSURE` (range `[2..2462]`, default
  in the script: `2400`) and `SENSOR_GAIN` (range `[128..2048]` step 128,
  default `1024`). These are written directly to the `ov8865` v4l2-subdev
  (discovered at runtime via `/sys/class/video4linux/v4l-subdev*/name`).
* **Software filter chain**: `BRIDGE_FILTER` is passed to ffmpeg as `-vf`.
  The default is:
  ```
  colorchannelmixer=rr=1.2:gg=0.95:bb=1.3,eq=brightness=0.12:contrast=1.15:saturation=0.92:gamma=1.1,format=yuv420p
  ```
  which boosts R and B, drops G, and adds brightness/contrast/gamma to
  compensate for the dark raw output. Tune the values to your liking.

To make the settings stick, create an override:

```bash
sudo systemctl edit surface-camera-bridge
```

and add:

```
[Service]
Environment=SENSOR_EXPOSURE=2462
Environment=SENSOR_GAIN=2048
Environment=BRIDGE_FILTER=colorchannelmixer=rr=1.15:gg=0.95:bb=1.25,eq=brightness=0.15:contrast=1.2,format=yuv420p
```

For a *real* fix, the libcamera IPU3 IPA itself needs per-sensor CCM,
LSC, and AGC targets tuned with a gray card and a known-temperature
light source. That's C++ work in `src/ipa/ipu3/` upstream and is
substantially out of scope for this contrib.

## Known limitations

* **Front camera only.** The script hard-codes the front camera's ACPI
  path (`\_SB_.PCI0.LNK1`). Change `CAMERA_NAME` to `\_SB_.PCI0.LNK0` for
  the rear camera.
* **One reader at a time.** `v4l2loopback` serializes simultaneous
  capture-side openers — if Zoom is using the camera, Chrome can't also
  open it at the same time. This matches real webcam behavior.
* **~100ms startup latency** between a client opening `/dev/video42` and
  the bridge actually producing real frames; the client sees idle black
  frames during that window. This is indistinguishable from a slow-to-
  unmute webcam.
* **CPU cost while active**: gstreamer + ffmpeg consume ~40% of one
  Pentium Gold core when running. The watcher demand-activates the bridge
  only when needed, so idle battery impact is a few hundred kB of python
  polling `/proc` every 100ms (measurably less than 1% CPU).
* **`exclusive_caps=0`** is required on v4l2loopback; `exclusive_caps=1`
  breaks the initial-STREAMON handshake for some readers. The provided
  `modprobe/v4l2loopback.conf` sets this correctly.
* **Secure Boot**: the unsigned v4l2loopback module will be rejected if
  Secure Boot is on. Either disable Secure Boot or enroll your own MOK
  and sign the built module.

## References

* [linux-surface#1569][1569] — "ov8865.yaml file missing" tracking issue
* https://github.com/umlaeute/v4l2loopback — v4l2loopback upstream
* https://git.libcamera.org/libcamera/libcamera.git/ — libcamera upstream
* https://libcamera.org/getting-started.html — IPU3 bring-up guide
