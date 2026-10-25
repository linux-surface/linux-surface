# PR #2289 — local update notes (do not push until SB3 smoke OK)

## Summary
- Rebased onto `origin/master` (7.2.12-1). Stock `patches/7.2/*` / configs taken from master — overlay stays in `contrib/sb3-ipu4-cameras/`.
- Default build: **7.2.12-surface-ipu4** (`downstream-legacy` tip) + SB3 DPHY + loafecho PM_QOS 100 µs.
- Dual-track 7.3: `v7.3-surface` @ `1ea399d` + ruslanbay `upstream` + 0014/0015 (settle **880/1400**, header RX) + loafecho 0016/0017.
- Userspace: pin settle 880/1400; removed 1500/800 thrash and white-frame “fix” drops.

## Test plan (SB3)
- [ ] Boot `*-surface-ipu4` (7.2.12 or 7.3 overlay)
- [ ] Front-HQ 2592×1944: no series of `DPHY non-recoverable` after lock
- [ ] Discord / guvcview: no grey screen, no periodic white flash
- [ ] Front↔Back switch: black loading, then image
- [ ] `dmesg`: no `stream stop/close time out` on stop (7.3)
