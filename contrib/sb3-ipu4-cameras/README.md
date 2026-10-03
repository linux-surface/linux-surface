# Surface Book 3 — IPU4P cameras (RGB profiles + IR / Howdy)

linux-surface still lists Book 3 cameras as unsupported. This overlay does
**not** belong in `patches/6.19/` — that series is INT3472 / IPU3 probe
ordering and has no `intel_ipu4p` sources.

RGB Front (OV5693) and Back (OV8865) plus IR (OV7251) are exposed as ordinary
V4L2 webcams through **7** v4l2loopback devices + `surface_webcamd`
(SP7-style Standard / HQ / Fast profiles). Default is
`Surface-Front-Standard` (native 4:3, no FHD stretch). IR is native
**480×640** YUYV. Raw IPU nodes stay root-only.

Howdy uses `Surface-IR-Howdy` only. See **[HOWDY.md](HOWDY.md)** for PAM,
enroll UI, LED behaviour, profiles, AE/AF, and first-boot safety.

## Apply order (kernel)

1. linux-surface `patches/6.19/*.patch`
2. [ruslanbay/ipu4-drivers](https://github.com/ruslanbay/ipu4-drivers) `patches/kernel/v6.19/*.patch`
3. `kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch` (+ IR timing as documented in HOWDY.md)

## Userspace

| Device label | Sensor | Output YUYV | Aspect | Target FPS |
|---|---|---:|---|---:|
| `Surface-Front-Standard` (default) | OV5693 | 1296×972 | 4:3 | 30 |
| `Surface-Front-HQ` | OV5693 | 2592×1944 | 4:3 | 15 |
| `Surface-Front-Fast` | OV5693 | 1296×728 | 16:9 | 60 |
| `Surface-Back-Standard` | OV8865 | 1632×1224 | 4:3 | 30 |
| `Surface-Back-HQ` | OV8865 | 3264×2448 | 4:3 | 15 |
| `Surface-Back-Fast` | OV8865 | 800×450 | 16:9 | 60 |
| `Surface-IR-Howdy` | OV7251 | 480×640 | 3:4 | ~30 |

IPU4 streams **one sensor at a time**. Webcamd switches when an app holds a
loopback. Continuous AE on Front/Back; live contrast AF on Back (DW9719).
IR LED is steady while IR streams (Howdy or preview).

## Install (Fedora, RHEL, Debian, Ubuntu, Arch, openSUSE)

Enable cameras in Surface UEFI (Volume Up + Power → Devices).

```bash
sudo ./install.sh
```

Everything installs under FHS paths (`/opt/surface-cameras`, `/usr/local/sbin`,
`/usr/local/bin`, `/etc/systemd/system`, …). No home-directory paths.

After install:

```bash
sudo configure-howdy-ir.sh          # valid Howdy INI + PAM
surface-howdy-add-profile           # English “Add Face Profile” app
```

Menu entry: **Howdy — Add Face Profile**.

## Layout

```
install.sh                 one-command installer (detects dnf/apt/pacman/zypper)
HOWDY.md                   Howdy / IR + RGB profile behaviour
kernel/                    firmware + kernel build
userspace/                 → /opt/surface-cameras
sbin/ bin/ desktop/ systemd/ udev/ modprobe.d/ selinux/
```

## Safety

- No face model → Howdy exits in &lt;1s; password login always works
- Greeter/lock: `wait-auth` nudges IR then `pam_howdy`; password is fallback
- PAM session hooks never poke sensor I2C or kill loopbacks
- SoftISP / libcamera is not the webcam path; intel-ipu6 stays off (CSE fight)
