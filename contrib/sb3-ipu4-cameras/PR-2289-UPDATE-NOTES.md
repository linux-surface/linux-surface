# PR #2289 update notes (post-rebase → 7.2.15 / 7.3.1)

- Rebased onto `origin/master` (BASE **v7.2.15**, `patches/7.3` BASE **v7.3.1**). Overlay stays in `contrib/sb3-ipu4-cameras/`.
- Default build: **7.2.15-surface-ipu4** (`downstream-legacy`) + SB3 DPHY + loafecho PM_QOS 100 µs.
- Also builds: **7.2.13**, **7.2.14**, **6.19.8**, **7.3.1** (patches mode), **7.3.0** (legacy v7.3-surface tip).
- #1353 ingest: WirePlumber `create-device.lua` advance; IPA CCM YAML; OV7251 link-freq (0018) + optional strobe (0019).
- Front CSI timing mode **2** (Discord/LED); no post-close LED hold; IPTS `CONFIG_HID_IPTS=m` enforced.
- Review (danielzgtg): udev-only perms — already addressed; systemd cam-perms unit removed.

## After reboot checklist

- [ ] Boot `*-surface-ipu4` (7.2.15 or 7.3.1)
- [ ] Front/Back/IR labels present; Discord first-open stable
- [ ] Howdy IR enroll; IPTS touch/pen; no PowerDevil nan% on kbd backlight
