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
- Working libcamera stack (`cam -l` should list the front camera)
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
./install.sh --always-on   # enable relay at boot (camera LED may stay lit)
./install.sh --uninstall   # remove everything this script installed
```

What it installs:

- `v4l2loopback` device **Surface Front Camera** (prefers `/dev/video0`)
- systemd service `surface-camera.service` (libcamera → loopback relay)
- `/usr/local/bin/app-with-camera.sh` (refcount-safe on-demand wrapper)
- Desktop / menu launchers for Zoom, Teams, WeChat, WhatsApp when those
  Flatpaks are present; stock Flatpak `.desktop` Exec lines are also wrapped

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

- Raw `ipu3-*` V4L2 nodes are hidden from apps (root/libcamera still has access)
  so conferencing UIs only list the virtual webcam.
- Cheese and other direct libcamera users may conflict while the relay is running.
- Electron/Chromium apps only enumerate the loopback while a producer is active
  (`exclusive_caps`).
- Front camera ACPI id is auto-detected via `cam -l` into
  `/etc/default/surface-camera` (override there if needed).
- This targets the IPU3 + libcamera path. IPU6 devices that fill
  `/dev/video0`–`63` may need a different `video_nr` / load-order strategy; see
  linux-surface discussions and issue #2242.

## Uninstall

```bash
./install.sh --uninstall
```
