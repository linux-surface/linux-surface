# 7.3.x SB3 IPU4 overlay patches (upstream track)

Base stack (see `kernel/build-and-install-kernel.sh`):

**Preferred (7.3.1):**

1. vanilla **7.3.1** + linux-surface `patches/7.3/` (repo BASE **v7.3.1**)
2. Sakari Ailus IPU6→IPU7 lore (`b4 shazam`)
3. ruslanbay/ipu4-drivers **`upstream`** tip (`00*.patch` or RFC `v2-00*.patch`)
4. This directory (SB3 overlays + loafecho + IR fixes from #1353)

**Legacy (7.3.0):** linux-surface/kernel `v7.3-surface` @ `1ea399d` via `SURFACE_MODE=rebase-kernel`.

Do **not** mix ruslanbay `downstream-legacy` (`intel_ipu4p_*`) into this track.
Userspace: libcamera / `surface_lc_webcamd` (loopback profiles) or native PipeWire
(#1353 — @JValdivia23 reports zero-daemon SoftISP on SB3).

| Patch | Purpose |
|-------|---------|
| 0007 | OV7251 INT3472 `avdd` + optional GPIOs |
| 0013 | OV5693 `MIPI_CTRL00` module param |
| 0014 | Fixed CSI timing for ports 0–2 |
| 0015 | BB8 for IR + lane-safe TERMEN; front timing modes |
| 0016 | loafecho: IPU4P `ISYS_PM_QOS` 100 µs |
| 0017 | loafecho: iwake watermark **after** `set_streaming(0)` |
| 0018 | OV7251 use matched link-freq index (I232826 / #1353) |
| 0019 | OV7251 optional strobe IR emitter (`strobe_pattern=`) |

**Front userspace:** pin CSI **timing mode 2** on SB3 (mode 0 / 1400/880 flaky).

**Intentionally skipped on upstream** (downstream-only): 0001 DPHY (`ipu4p-isys-csi2.c`), 0002–0006 API shims, 0008 `set_dma_ops`.

**Boot note:** refuse builds without BTRFS + NVMe; start from Fedora `kernel-x86_64-fedora.config`.
