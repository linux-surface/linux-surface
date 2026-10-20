# Pre-commit checklist (7.2.11 vs 6.19.8-3)

Do **not** commit until every item is checked.

## Upstream trees

- [x] linux-surface `patches/7.2/` + `configs/surface-7.2.config` imported from `arch/debian/fedora-*-7.2.11-1` / origin/master
- [x] ipu4-drivers: for **7.2.11** use **downstream-legacy** `patches/kernel/v6.19/` (fetched latest)
- [x] ipu4-drivers **upstream** reviewed: targets **7.3-rc6 / IPU6** — documented as not for 7.2.11 builds

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

- [x] English-only comments in shipped overlay (canon)
- [x] No `/home/<user>`, `Pulpit/cursor`, or `/run/user/1000` hard-codes in canon
- [x] Distro-neutral `install.sh` (dnf/apt/pacman/zypper)
- [x] One-click: `sudo ./install.sh` (+ optional `SURFACE_IPU4_KVER`)
- [x] Howdy / face profile docs (`HOWDY.md`)
- [x] remote-desktop helpers for 7.2 (grant/wait/publish/tray/viewer-flag; greeter attach = **WIP**)
- [x] Trees synced: `contrib/sb3-ipu4-cameras` ↔ `surface-cameras-production-ready` ↔ package dirs in `surface-cameras`

## Explicitly open

- Chrome Remote Desktop on greeter / lock screen after boot — **not fixed**; helpers only
