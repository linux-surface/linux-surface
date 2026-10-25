# 7.3.0 SB3 IPU4 overlay patches (upstream track)

Base stack (see `kernel/build-and-install-kernel.sh`):

1. linux-surface/kernel **`v7.3-surface` @ `1ea399d`** (7.3.0-rc7; no `patches/7.3` in linux-surface repo yet)
2. Optional rebase onto newer upstream `v7.3.x` when available
3. Sakari Ailus IPU6→IPU7 lore series (`b4 shazam`)
4. ruslanbay/ipu4-drivers branch **`upstream`** → `patches/00*.patch` (tip)
5. This directory (SB3 portable overlays + loafecho #1353)

Do **not** mix ruslanbay `downstream-legacy` (`intel_ipu4p_*`) into this track.
Userspace for upstream is libcamera / V4L2 on `intel-ipu6`, not the SHM webcamd path.

| Patch | Purpose |
|-------|---------|
| 0007 | OV7251 INT3472 `avdd` + optional GPIOs (ported from 7.2.12) |
| 0013 | OV5693 `MIPI_CTRL00` module param (sensor half of 7.2.12 DPHY work) |
| 0014 | Fixed CSI timing for ports 0–2 (broaden SP7 port-2 quirk) |
| 0015 | BB8 for IR + lane-safe TERMEN; front header-layout SETTLE **880/1400** (default mode 0) |
| 0016 | loafecho: IPU4P `ISYS_PM_QOS` 100 µs (C2 stalls CSI) |
| 0017 | loafecho: iwake watermark **after** `set_streaming(0)` (stop/close timeouts) |

**IR status (0015):** OV7251 delivers real frames after BB8 AFE + stopping the 8-lane TERMEN smash.
**Back:** CSI OK; raw path needs high `exposure`/`analogue_gain` (defaults 32/128 → black). Handled in `surface_lc_webcamd.py`.
**Front:** pin settle 880/1400 on port-2 *header* RX layout (yodabot); do not filter white frames in userspace.

**Intentionally skipped on upstream** (downstream-only):

| Old patch | Why skipped |
|-----------|-------------|
| 0001 DPHY lock (ipu4p-isys-csi2.c) | Superseded by 0014/0015 on `ipu4p-isys-csi-phy.c` |
| 0002–0006 API shims | `intel_ipu*` downstream only |
| 0008 `set_dma_ops` / `ARCH_HAS_DMA_OPS` | downstream `intel_ipu` DMA path |

**Boot note:** a previous 7.3.0 build used a broken minimal `.config` (`BTRFS`/`NVMe` unset).
The build script now starts from Fedora `kernel-x86_64-fedora.config` and refuses to build if those stay off.
