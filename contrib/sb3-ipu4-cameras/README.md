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

Supported camera kernels (Fedora 44/45, Nobara 44):

| Base | linux-surface | IPU4 | SB3 DPHY |
|------|---------------|------|----------|
| **6.19.8** | `patches/6.19/` | ruslanbay **downstream-legacy** `v6.19` | `kernel/patches/0001-…-dphy-lock.patch` |
| **7.2.11** | `patches/7.2/` | same `v6.19` IPU4 series (applies cleanly) | same DPHY patch |
| **7.3.x** | when `patches/7.3/` ships | ruslanbay **upstream** (IPU6-based, targets ≈v7.3-rc6) | TBD |

**Important:** for **7.2.11** keep using `downstream-legacy` + `patches/kernel/v6.19/`. The
[ipu4-drivers `upstream`](https://github.com/ruslanbay/ipu4-drivers/tree/upstream) branch is a
different stack (IPU6) aimed at 7.3+ — do **not** mix it into a 7.2.11 build.

Stock `kernel-surface` RPMs (e.g. `fedora-*-7.2.11-1`) do **not** include IPU4P — only INT3472/IPU3 camera quirks.

Profiles (capture / publish / FPS) must match the last known-good **6.19.8-3** overlay —
do not change resolutions, FPS, or ISYS timeouts without an explicit regression pass.

Build:

```bash
# default: 7.2.11-surface-ipu4
sudo bash kernel/build-and-install-kernel.sh
# or keep the previous LTS tree:
sudo SURFACE_IPU4_KVER=6.19.8 bash kernel/build-and-install-kernel.sh 6.19.8
```

Stack on either base:

1. linux-surface `patches/<series>/*.patch`
2. [ruslanbay/ipu4-drivers](https://github.com/ruslanbay/ipu4-drivers) branch `downstream-legacy` → `patches/kernel/v6.19/*.patch`
3. `kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch` (+ IR timing in HOWDY.md)

On **7.2.11** the downstream IPU4 series needs small API shims (also under
`kernel/patches/7.2/`): uapi `stdint.h` → `linux/types.h`, clk `round_rate` →
`determine_rate`, drop removed `vb2_ops.wait_prepare/wait_finish`, and set
`aq->vbq.lock = &av->mutex` (Linux 7.x `vb2_core_queue_init` requires it —
without this, ISYS probe fails with `can't init video node`, Oopses in
`vb2_queue_release` cleanup, and no `/dev/video*`). Patch `0006` clears
`vbq.ops` on failed init so cleanup is safe.

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
kernel/                    firmware + kernel build (linux-surface 7.2.11-1 + IPU4)
userspace/                 → /opt/surface-cameras
sbin/ bin/ desktop/ systemd/ udev/ modprobe.d/ selinux/
remote-desktop/            Chrome Remote Desktop / portal helpers (greeter attach = WIP)
```

## Remote desktop

See **[remote-desktop/README.md](remote-desktop/README.md)**. Portal grant, greeter env
publish, wait-display, and tray-icon helpers are universal (no hard-coded login uid).
Desktop share needs MONITOR ScreenCast (see `remote-desktop/NOTES-MONITOR.md`).
Greeter / lock-screen share is still **WIP** on 7.2.11.

## Safety / regression guardrails

- No face model → Howdy exits in &lt;1s; password login always works
- Greeter/lock: typed password skips face scan; empty field may use Howdy (PAM order)
- PAM session hooks never poke sensor I2C or kill loopbacks
- SoftISP / libcamera is not the webcam path; intel-ipu6 stays off (CSE fight)
- Never hard-code `/run/user/<login-uid>`, `$HOME/…/Pulpit/…`, or a username in scripts
- Build tree: `SRC_ROOT` / `SURFACE_IPU4_BUILD` or `$HOME/src/surface-ipu4-build` (FHS-friendly)
- One-click update: re-run `sudo ./install.sh` (rebuilds kernel only if not already on `*-surface-ipu4`)
- Audio / NVIDIA: install hooks keep PipeWire audio paths and do not hard-require a dGPU unit
