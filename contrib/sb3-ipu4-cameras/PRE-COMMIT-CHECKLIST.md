# Pre-commit checklist (7.2.15 / 7.3.1 vs 6.19.8-3)

Do **not** commit until every item is checked.

## Upstream trees

- [x] linux-surface `patches/7.2/` + configs from origin/master (**BASE v7.2.15**)
- [x] linux-surface `patches/7.3/` present (**BASE v7.3.1**)
- [x] Overlay default build: **7.2.15-surface-ipu4**; also 7.2.13 / 7.2.14
- [x] ipu4-drivers **downstream-legacy** for 6.19.8 / 7.2.13–15 webcamd path
- [x] ipu4-drivers **upstream** (RFC v2) for 7.3.1 / 7.3.0 libcamera path
- [x] #1353 ingest: WP create-device.lua advance, IPA CCM YAML, OV7251 link-freq + strobe
- [x] No `/home/<user>`, username, or `Pulpit/…` hard-codes; English-only in canon

## Profiles (must match 6.19.8-3)

| Label | cap | out | fps |
|-------|-----|-----|-----|
| Front-Standard | 1296×972 | 1296×972 | 30 |
| Front-HQ | 2592×1944 | 1920×1440 | 15 |
| Front-Fast | 1296×972 | 1296×728 | 60 |
| Back-Standard | 1632×1224 | 1632×1224 | 30 |
| Back-HQ | 3264×2448 | 1920×1440 | 15 |
| Back-Fast | 800×600 | 800×450 | 60 |
| IR-Howdy | 640×480 | 480×640 | 30 |

## Package quality

- [x] Distro-neutral `install.sh` (dnf/apt/pacman/zypper)
- [x] One-click: `sudo ./install.sh` (+ optional `SURFACE_IPU4_KVER`)
- [x] Howdy / face profile docs (`HOWDY.md`)
