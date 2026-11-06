# Surface virtual camera (libcamera → v4l2loopback)

One-click installer that exposes a Surface IPU3 / libcamera front camera as a
normal V4L2 webcam named **Surface Front Camera**, so Zoom, Teams, WeChat,
WhatsApp (ZapZap), browsers, and other Electron apps can use it.

Mainstream conferencing apps are not libcamera-aware. This bridge loads
`v4l2loopback` with `exclusive_caps=1`, relays frames from `libcamerasrc`, and
starts the relay on demand when you open a wrapped app.

Maintained copy (issues / newer revisions):
https://github.com/lixiaowww/surface-virtual-camera

## Requirements

- Ubuntu / Linux Mint / Debian (uses apt + DKMS)
- Working libcamera stack (`cam -l` should list the front camera before install,
  or when using `--no-hide-raw`)
- [linux-surface](https://github.com/linux-surface/linux-surface) kernel recommended
- sudo (installs DKMS module, systemd unit, udev rules, desktop wrappers)

Tested on Surface Pro 4 (IPU3 / ov5693 front camera) with linux-surface 7.2.x.

## Installation

```bash
cd contrib/surface-virtual-camera
chmod +x install.sh
./install.sh
```

Options:

```bash
./install.sh --always-on     # enable relay at boot (camera LED may stay lit)
./install.sh --no-hide-raw   # leave ipu3-* accessible to user libcamera / PipeWire
./install.sh --uninstall     # remove everything this script installed
```

What it installs:

- `v4l2loopback` device **Surface Front Camera** (prefers `/dev/video0`)
- systemd service `surface-camera.service` (libcamera → loopback relay)
- `/usr/local/bin/app-with-camera.sh` (refcount-safe on-demand wrapper)
- Desktop / menu launchers for Zoom, Teams, WeChat, WhatsApp when those
  Flatpaks are present; stock Flatpak `.desktop` Exec lines are also wrapped
- Runs `update-initramfs -u` so `softdep ipu3_cio2 pre: v4l2loopback` is present
  in the initrd (otherwise early-boot load order may not claim `/dev/video0`)

Default mode is **on-demand**: the sensor stays off until a wrapped app starts,
and stops when the last wrapped app exits.

## After install

1. Open Zoom / Teams / WeChat / WhatsApp from the menu (or the Desktop
   “Camera” launchers).
2. In the app’s video settings, select **Surface Front Camera**.
3. If `/dev/video0` is not the virtual device yet, reboot once so module load
   order applies (`v4l2loopback` before IPU3).

Manual control:

```bash
sudo systemctl start surface-camera
sudo systemctl stop surface-camera

# Wrap any Flatpak or command:
app-with-camera.sh flatpak com.example.App
app-with-camera.sh run firefox
```

## Notes / caveats

### Raw `ipu3-*` permissions (default)

By default `99-hide-ipu3-v4l.rules` sets `ipu3-cio2*` / `ipu3-imgu*` to
`MODE=0600`, `GROUP=root`, and strips `uaccess`. The root relay can still open
libcamera; **unprivileged** `cam -l`, WirePlumber `monitor.libcamera`, and the
PipeWire Camera portal **cannot** open the raw sensors. That is intentional so
Zoom/Electron do not enumerate unusable V4L2 nodes.

Reinstall with `./install.sh --no-hide-raw` to keep native libcamera/PipeWire
access. On IPU6 devices that fill `/dev/video0`–`63`, prefer a per-process
`LD_PRELOAD` remapper such as
[surface-zoom-bridge](https://github.com/luqqas96/surface-zoom-bridge) instead
of globally locking down nodes; see also linux-surface issue #2242.

### Other

- Cheese and other direct libcamera users may conflict while the relay is running.
- Front camera ACPI id is auto-detected via `cam -l` (as root during install)
  into `/etc/default/surface-camera`.
- Electron/Chromium apps only enumerate the loopback while a producer is active
  (`exclusive_caps`).

## Uninstall

```bash
./install.sh --uninstall
```
