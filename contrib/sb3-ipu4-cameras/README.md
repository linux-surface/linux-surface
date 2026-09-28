# Surface Book 3 — IPU4P RGB cameras

linux-surface still lists Book 3 cameras as unsupported. This overlay does
**not** belong in `patches/6.19/` — that series is INT3472 / IPU3 probe
ordering and has no `intel_ipu4p` sources. Applying the DPHY lock patch on
vanilla linux-surface would fail.

RGB Front (OV5693) and Back (OV8865) are exposed as ordinary V4L2 webcams
(`Surface-Front`, `Surface-Back`) through v4l2loopback + a userspace daemon.
Browsers and apps see 1920×1080 YUYV. Raw IPU nodes stay root-only.

IR / Howdy (OV7251) plumbing is present (loopback name, udev, sensor bind)
but **STREAMON on IR is disabled**. IR STREAMON currently kills IPU4 ISYS.
That work is left for a later commit.

## Apply order (kernel)

1. linux-surface `patches/6.19/*.patch`
2. [ruslanbay/ipu4-drivers](https://github.com/ruslanbay/ipu4-drivers) `patches/kernel/v6.19/*.patch`
3. `kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch`

```bash
git am contrib/sb3-ipu4-cameras/kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch
```

## What the kernel patch changes

Surface Book 3 front OV5693, CSI-2 port 2, 2-lane, 419.2 MHz:

- no AFE pulse on CSI enable (8–10 ms RX=0 breaks the first LP→HS)
- `rx_config = RELEASE_LP11` only — do **not** set `DISABLE_BYTE_CLK_GATING`
  (sensor `MIPI_CTRL00=0x2d` + byte-clk-gate → 2–11 lines then 0xFF)
- settle 684/661 (Windows/CIO2 calc), not 1392 and not Surface Pro 7 1155/1269
- AFE pulse on disable only (leftover HS 0x302 after STREAMOFF)
- OV5693 `MIPI_CTRL00=0x2d` (Windows non-continuous clock)
- CSE auth retry wait 8 s after the first BOOT_LOAD

Modprobe (shipped in `modprobe.d/`):

```
options ov5693 mipi_ctrl00=0x2d
options intel_ipu4p_isys sb3_front_timing_quirk=1 sb3_front_clk_ticks=684 sb3_front_data_ticks=661
```

## Userspace

| Device | Sensor | Default node |
|--------|--------|----------------|
| Front RGB (default) | OV5693 | `/dev/video60` (`Surface-Front`) |
| Back RGB | OV8865 | `/dev/video61` (`Surface-Back`) |
| IR Howdy | OV7251 | hidden — STREAMON disabled |

IPU4 can stream **one sensor at a time**. The daemon switches Front/Back when
an app actually holds the loopback. The privacy LED is on only while a reader
exists.

`v4l2loopback` is loaded with `exclusive_caps=1`. The writer must keep
`V4L2_CAP_VIDEO_CAPTURE` set or the device vanishes from guvcview/browsers.
The daemon reopens the writer if CAPTURE is lost (for example after a fast
app close).

## Install (Fedora, RHEL, Debian, Ubuntu, Arch, openSUSE)

Enable cameras in Surface UEFI (Volume Up + Power → Devices).

```bash
sudo ./install.sh
```

First run builds `6.19.8-surface-ipu4` if that kernel is not already booted
(long). Reboot and pick the `*surface-ipu4*` entry. A second `sudo ./install.sh`
on that kernel only installs userspace and starts the services.

Everything installs under FHS paths (`/opt/surface-cameras`, `/usr/local/sbin`,
`/etc/systemd/system`, `/etc/udev/rules.d`, `/etc/modprobe.d`). No home
directory paths.

## Layout

```
install.sh                 one-command installer (detects dnf/apt/pacman/zypper)
kernel/                    firmware + kernel build (linux-surface + ipu4-drivers + DPHY lock)
userspace/                 → /opt/surface-cameras
systemd/  sbin/  udev/  modprobe.d/  modules-load.d/  wireplumber/
```

## Intentionally disabled (this commit)

- Howdy / IR STREAMON (OV7251) — ISYS dies; planned for a later commit
- SoftISP / libcamera as the webcam path
- intel-ipu6 (CSE/MEI fight with IPU4)
