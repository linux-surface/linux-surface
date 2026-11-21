# Surface Laptop 1: optional infrared camera for PipeWire

This is a user-session camera source for the built-in monochrome OV7251 on the
**first-generation Microsoft Surface Laptop**. It gives PipeWire camera apps a
640×480 view. The default is ambient light at 30 fps. With a compatible kernel
driver, you can explicitly select synchronized illumination at 15 fps. The
OV9734 color webcam is separate; installing this tool does not enable or
change it. No face authentication, virtual kernel device or root daemon is
involved.

The kernel needs to bind the OV7251 and expose its Intel CIO2 media route and
packed 10-bit monochrome format. The IPU3 pipeline in
[libcamera](https://gitlab.freedesktop.org/camera/libcamera/-/blob/adfa719f8ed67d18fd8a0739fc088a1f8b9e7252/src/libcamera/pipeline/ipu3/cio2.cpp#L30)
accepts Bayer inputs only. This bridge reads that separate sensor through V4L2.
Illumination requires the writable OV7251 `strobe_output_enable` control and a
driver exposing V4L2 analogue gain which bounds the pulses and disarms them at stream stop. The
[linux-media submission](https://lore.kernel.org/linux-media/20261113024059.1087031-9-antoine.pichot@gmail.com/)
and [linux-surface issue #739](https://github.com/linux-surface/linux-surface/issues/739)
track the work. The
[linux-surface 7.3 camera patch set](https://github.com/linux-surface/linux-surface/blob/a88004b01334adfd1d2957dc646a2a2664ddebb8/patches/7.3/0013-cameras.patch)
does not add that control. An unmodified upstream strobe series has not been
validated on this laptop; measurements below used a 7.3 backport with explicit
disarming. This tool contains no kernel patch and never programs raw registers.

## Build and verify

Install a C compiler, Linux media userspace headers (compatible with 7.3),
pkg-config, PipeWire/SPA development files, GNU make and coreutils, Python 3
and `media-ctl` (v4l-utils). To publish the source you also need the current
user's PipeWire server and camera session manager. The optional user unit
requires systemd; `notify-send` is optional. Use your normal graphical-session
user with camera access; do not run this camera as root.

```sh
make
make check
./surface-laptop1-ir.py --probe
./surface-laptop1-ir.py --check
```

`make check` uses synthetic packed frames, simulated device discovery and
POSIX locks; it does not access cameras. `--probe` inspects the graph and
prints the nominal output rate without starting capture. `--check` captures
ten timestamped output frames in memory and reports their measured rate and
brightness range; it saves no pictures. Leave other camera apps closed. In a
room with little near-infrared light, passive output can look dark or noisy.

On a driver supporting synchronized illumination:

```sh
./surface-laptop1-ir.py --check --illuminate
./surface-laptop1-ir.py --webcam --illuminate
```

Select **Surface Infrared (monochrome)** in Camera. Stop the foreground source
with Ctrl+C. `--illuminate` fails clearly if the control is missing: it does
not substitute a passive image. For ambient-only use, omit `--illuminate`.

## Optional installation and removal

```sh
sudo make install
systemctl --user daemon-reload
systemctl --user enable --now surface-laptop1-ir.service
```

This installs under `/usr/local`. Packaging supports `prefix`, `bindir`,
`libexecdir`, `userunitdir` and `DESTDIR`; for example,
`make DESTDIR="$PWD/stage" prefix=/usr install` creates a package tree without
touching system directories. The installed command is `surface-laptop1-ir`.
Installation alone neither enables the unit nor opens a camera. The unit
publishes the passive source. For illuminated use with a tested driver,
`systemctl --user edit surface-laptop1-ir.service` and add:

```ini
[Service]
Environment=SURFACE_IR_MODE=--illuminate
```

Then run `systemctl --user restart surface-laptop1-ir.service`. To revert to
ambient light, remove that override or set `SURFACE_IR_MODE=--passive`.
To remove the bridge, first remove any override you created using
`systemctl --user revert surface-laptop1-ir.service`, then:

```sh
systemctl --user disable --now surface-laptop1-ir.service
sudo make uninstall
systemctl --user daemon-reload
```

Use the same Make directory settings you used to install it.

## Capture behavior and diagnosis

- The public launcher checks all three DMI identifiers (vendor, model and
  SKU), the Intel CIO2 driver, IPU3 graph and the sensor's firmware HID.
  It rejects other Surface generations, missing or ambiguous matches, and
  discovers all device nodes by their links. The known SL1 mounting rotation
  is applied in software. The native mode used is 640×480 at 30 Hz; a
  different frame interval, vertical blanking or active test pattern is refused.
- Publishing a node does not start capture. While an app streams, the bridge
  holds the same CIO2 media-device POSIX lock as libcamera. The driver uses one
  capture channel: use either sensor at a time. A requested IR PipeWire stream
  waits for the color client; a direct check reports busy. Raw tools that
  ignore the advisory lock are not protected.
- The light is requested only after the bridge owns a running queue, and
  cleared before stopping it. The sensor pulses on alternate exposures. The
  bridge selects the brighter original 10-bit frame in each adjacent pair,
  preserves its timestamp, and restarts pairing after a dropped sequence.
  Movement or very bright ambient light can make that brightness test
  ambiguous. The desktop format is YUY2 grayscale.
- There is no independent kernel-exposed IR privacy light on the tested SL1.
  Start/stop messages go to the service journal and to desktop notifications
  if available. The color camera's visible privacy LED is separate. After a
  forcible capture-process exit, shutdown of the strobe is a kernel
  responsibility; userspace cannot run cleanup after SIGKILL.
- For a missing source, check `surface-laptop1-ir --probe`,
  `wpctl status` and `journalctl --user -u surface-laptop1-ir.service`.
  For a missing emitter control, use passive mode. Keep only one IR publishing
  service active. No changes to camera permissions, Secure Boot or the installed
  color-camera stack are required by the bridge.

## Evidence

On a first-generation Surface Laptop with Fedora 45, kernel
`7.3.4-2.surface.fc45.x86_64` and a model-scoped OV7251 backport,
active capture verified 640×480 at 15 illuminated fps, and passive capture
30 fps. In a separate fixed-exposure test, central raw brightness in
0–1023 units measured **20.6 off → 251.9 illuminated → 20.5 off**. The dark
interleaved exposures measured 20.7; all 17 usable central regions responded
in phase. A second run found the same alternation in 13 out of 13 regions.
The kernel reported strobe disabled after graceful close and after either the
IR capture process or a desktop client was killed. The exact staged package
delivered 12 timestamped YUY2 frames in each mode to a GStreamer app on a
private PipeWire server: 30.0 fps passive with strobe always off, 15.0 fps
illuminated with strobe off before/after capture. A separate direct check of
this package measured ten frames at each rate, with control values restored.
GNOME Camera and both camera switching orders were also tested with the
precursor of this package.
These are observations on one unit; they are not a certification of other
models, drivers, suspend behavior, or face authentication.

The code is MIT-licensed. Its stream setup is derived from PipeWire's
[video source example](https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/4287f31510771fc999f8a58a024c81ca10b8d55f/src/examples/video-src.c);
the original attribution and permission notice are in [LICENSE](LICENSE).
