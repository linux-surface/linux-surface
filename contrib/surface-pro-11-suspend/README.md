# Surface Pro 11 (Intel) Suspend Fix

## Problem

On the Surface Pro 11 with Intel Lunar Lake, suspending (s2idle) with the display
still active causes an unrecoverable hang. The Intel Lunar Lake display/GPU does not
cleanly transition to a power-down state when s2idle is entered while the display
pipeline is active.

## Solution

A systemd service that blanks the display before `systemd-suspend.service` runs.
It locks all sessions (causing GNOME to blank the display via proper DPMS) and waits
2 seconds for the display hardware to fully power down.

This must be a systemd service (not a sleep hook) because sleep hooks run after
`user.slice` is frozen — at which point GNOME cannot process the DPMS request.

## Required kernel parameters

Add to GRUB (`/etc/default/grub`):
```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash acpi_sleep=nonvs acpi_osi=\"Windows 2020\""
```

Then run `sudo update-grub`.

## Installation

```bash
sudo cp surface-pre-suspend.sh /usr/local/bin/
sudo chmod +x /usr/local/bin/surface-pre-suspend.sh
sudo cp surface-display-off.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable surface-display-off.service
```

## Kernel patch

The lid wake GPE (0x2E) patch should also be applied — see
`patches/6.17/0022-surface-pro-11-intel-lid-wake.patch`.

## Wake sources

- **Power button**: works
- **Lid open**: works (requires GPE 0x2E patch + `surface_gpe` module)
- **Keyboard**: does not wake (Type Cover uses SAM/UART, not USB)
