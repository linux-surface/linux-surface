# Vendored ruslanbay IPU4P series for linux 7.3.x + Sakari

Taken from the known-good `v7.3-surface` bring-up (`format-patch` of the
12 IPU4P commits that produced a working SB3 `*-surface-ipu4` image).

Uses `IPU_VERSION_4P BIT(6)` (does **not** shift existing version bits).

Do **not** replace with tip `v2-00*.patch` from ruslanbay/ipu4-drivers for
vanilla 7.3.1 builds: RFC v2 targets media-committers `next` (IPU8 layout)
and fails `git am --3way` after Sakari on 7.3.1.
