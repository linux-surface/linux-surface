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

Supported camera kernels (Fedora 44/45, Nobara 44) — rebased on linux-surface master
(`patches/7.2` BASE **v7.2.15**, `patches/7.3` BASE **v7.3.1**):

| Base | linux-surface | IPU4 | SB3 overlay |
|------|---------------|------|-------------|
| **6.19.8** (optional LTS) | `patches/6.19/` | ruslanbay **downstream-legacy** `v6.19` | `kernel/patches/6.19/` |
| **7.2.13 / 7.2.14 / 7.2.15** | `patches/7.2/` | same `v6.19` tip + 7.x API shims | `kernel/patches/7.2/` (**default 7.2.15**) |
| **7.3.1** | `patches/7.3/` | ruslanbay **upstream** RFC v2 + Sakari lore | `kernel/patches/7.3/` |
| **7.3.0** (legacy tip) | [`v7.3-surface`](https://github.com/linux-surface/kernel) @ `1ea399d` | same upstream track | same SB3 overlays |

**Tracks (do not mix ABI in one install without a flag):**

- **6.19.8 / 7.2.13–15** — `downstream-legacy` + `intel_ipu4p_*` for SHM `surface_webcamd`.
- **7.3.1 / 7.3.0** — [ipu4-drivers `upstream`](https://github.com/ruslanbay/ipu4-drivers/tree/upstream) on IPU6 (`intel-ipu6`); libcamera / `surface_lc_webcamd` (and optional native PipeWire — see #1353 / @JValdivia23).

Stock `kernel-surface` RPMs do **not** include IPU4P — only INT3472/IPU3 camera quirks.

Profiles (capture / publish / FPS) for the webcamd path must match the last known-good
**6.19.8-3** overlay — do not change resolutions, FPS, or ISYS timeouts without a regression pass.

Build:

```bash
# default: 7.2.15-surface-ipu4 (downstream-legacy / webcamd)
bash kernel/build-and-install-kernel.sh
bash kernel/build-and-install-kernel.sh 7.2.14   # or 7.2.13
bash kernel/build-and-install-kernel.sh 6.19.8   # optional LTS
# upstream IPU6 track (libcamera / lc_webcamd):
bash kernel/build-and-install-kernel.sh 7.3.1
bash kernel/build-and-install-kernel.sh 7.3.0    # legacy v7.3-surface tip
```

### Front DPHY / community ingest (#1353)

- Front CSI **timing mode 2** (calc settle + Windows TERMEN). Mode 0 / 1400/880 is pure-white or flaky on SB3.
- **loafecho:** `ISYS_PM_QOS` 100 µs; iwake **after** `set_streaming(0)` (7.3).
- **yodabot / JValdivia23:** port-2 *header* RX layout; BB8 for IR; 2-lane shifted TERMEN/SETTLE map.
- **I232826:** OV7251 matched `link_freq` index (`0018`); optional strobe IR emitter (`0019`).
- **ConsultingFuture4200:** WirePlumber `create-device.lua` must `transition:advance()` when disabling V4L2 nodes; IPA CCM YAML for ov5693/ov8865.
- Do **not** drop white frames in userspace or thrash settle 880↔1500.

### 7.3.x upstream stack

**7.3.1 (preferred):** vanilla 7.3.1 + linux-surface `patches/7.3/` (BASE v7.3.1).

**7.3.0 (legacy):** linux-surface/kernel `v7.3-surface` @ `1ea399d`.

Then for both:

1. Sakari Ailus IPU6→IPU7 lore (`b4 shazam`)
2. ruslanbay `upstream` → `00*.patch` / RFC `v2-00*.patch`
3. SB3: OV7251 avdd (0007), OV5693 MIPI_CTRL00 (0013), CSI 0014/0015, loafecho 0016/0017, IR link-freq/strobe 0018/0019
4. Config from Fedora `kernel-x86_64-fedora.config` (must keep BTRFS + NVMe + **`CONFIG_HID_IPTS=m`**)

### Touch / pen (IPTS)

SB3 digitizer needs in-tree **`ipts.ko`** (`CONFIG_HID_IPTS=m`, depends on `INTEL_MEI`). A previous 7.3 surface-ipu4 build left IPTS unset (linux-surface `0005-ipts` reject on `mei/pci-me.c` while Fedora config had `# CONFIG_HID_IPTS is not set`), so iptsd never saw `/dev/hidraw` and touch/stylus died.

`kernel/build-and-install-kernel.sh` now:

- verifies `drivers/hid/ipts/` before `.config`
- enables `INTEL_MEI` + `HID_IPTS` and **refuses** the build if `.config` still lacks `CONFIG_HID_IPTS=m`

Until the next kernel rebuild, the OOT fallback is `tools/rebuild-ipts-module.sh` (sources in `/usr/local/src/ipts-oot`).

### 6.19 / 7.2 downstream stack

1. linux-surface `patches/<series>/` (7.2.15 on master; also 7.2.13 / 7.2.14)
2. ruslanbay `downstream-legacy` → `patches/kernel/v6.19/*.patch` (tip)
3. `kernel/patches/<series>/` — DPHY + API shims + OV7251 avdd + PM_QOS + IR 0018/0019

On **7.2.x** the downstream IPU4 series needs API shims: uapi `stdint.h` →
`linux/types.h`, clk `round_rate` → `determine_rate`, drop removed
`vb2_ops.wait_prepare/wait_finish`, and set `aq->vbq.lock = &av->mutex` (Linux 7.x
`vb2_core_queue_init` requires it — without this, ISYS probe fails with
`can't init video node`, Oopses in `vb2_queue_release` cleanup, and no `/dev/video*`).
Patch `0006` clears `vbq.ops` on failed init so cleanup is safe.

## Userspace

| Device label | Sensor | Output YUYV | Aspect | Target FPS |
|---|---|---:|---|---:|
| `Surface-Front-Standard` (default) | OV5693 | 1296×972 | 4:3 | 30 |
| `Surface-Front-HQ` | OV5693 | 1920×1440 (from 2592×1944) | 4:3 | 15 |
| `Surface-Front-Fast` | OV5693 | 1296×728 (from 1296×972) | 16:9 | 60 |
| `Surface-Back-Standard` | OV8865 | 1632×1224 | 4:3 | 30 |
| `Surface-Back-HQ` | OV8865 | 1920×1440 (from 3264×2448) | 4:3 | 15 |
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
kernel/                    firmware + kernel build (linux-surface 7.2.15 / 7.3.1 + IPU4)
userspace/                 → /opt/surface-cameras
sbin/ bin/ desktop/ systemd/ udev/ modprobe.d/ selinux/
remote-desktop/            Chrome Remote Desktop / portal helpers (greeter attach = WIP)
```

## Remote desktop

See **[remote-desktop/README.md](remote-desktop/README.md)**. Portal grant, greeter env
publish, wait-display, and tray-icon helpers are universal (no hard-coded login uid).
Desktop share needs MONITOR ScreenCast (see `remote-desktop/NOTES-MONITOR.md`).
Greeter / lock-screen share is still **WIP** on 7.2.15.

## Safety / regression guardrails

- No face model → Howdy exits in &lt;1s; password login always works
- Greeter/lock: typed password skips face scan; empty field may use Howdy (PAM order)
- PAM session hooks never poke sensor I2C or kill loopbacks
- SoftISP / libcamera is not the webcam path; intel-ipu6 stays off (CSE fight)
- Never hard-code `/run/user/<login-uid>`, `$HOME/…/Pulpit/…`, or a username in scripts
- Build tree: `SRC_ROOT` / `SURFACE_IPU4_BUILD` or `$HOME/src/surface-ipu4-build` (FHS-friendly)
- One-click update: re-run `sudo ./install.sh` (rebuilds kernel only if not already on `*-surface-ipu4`)
- Audio / NVIDIA: install hooks keep PipeWire audio paths and do not hard-require a dGPU unit
