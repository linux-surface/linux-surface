# 7.3.0 SB3 IPU4 overlay patches

Base: upstream `v7.3` (VERSION 7.3.0) + linux-surface `v7.3-surface`
rebased onto `v7.3` (41 commits, clean) + ruslanbay `downstream-legacy`
`patches/kernel/v6.19/` + this directory.

Do **not** use ruslanbay `upstream` (IPU6-based) for the webcamd SHM path;
that stack is a separate track.

| Patch | Purpose |
|-------|---------|
| 0001 | SB3 Front DPHY lock |
| 0002–0006 | Linux 7.x API shims (same as 7.2) |
| 0007 | OV7251 INT3472 avdd + optional GPIOs (refreshed for 7.3) |
| 0008 | `set_dma_ops()` + `select ARCH_HAS_DMA_OPS` (7.3 x86 gates `device::dma_ops`) |
