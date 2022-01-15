# Linux Patches for Microsoft Surface Devices

Patches are grouped in directory by kernel version and inside those directories split up in patch-sets based on their functionality.
They should be used with the appropriate config fragments from the `/configs/<version>` directory.

These patches are generated from the `-surface` branches of our [kernel repository][1].
Please direct any pull-requests for new patches and kernel functionality to this repository, targetting the latest `-surface-devel` branch.

## Maintained Versions

The currently maintained versions are
- [`5.16`](https://github.com/linux-surface/kernel/tree/v5.16-surface) (latest Arch Linux)
- [`5.15`](https://github.com/linux-surface/kernel/tree/v5.15-surface) (latest Ubuntu/Fedora)

Any other versions are only included for historical purposes.
Unmaintained versions will likely (if necessary with a bit of re-basing) still work, but will not have the latest changes (so please don't report bugs for those).

[1]: https://github.com/linux-surface/kernel
