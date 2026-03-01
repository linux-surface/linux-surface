# Surface Pro 11 (Intel) Suspend Fix

## Problem

On the Surface Pro 11 with Intel Lunar Lake, suspending (s2idle) with the display
still active causes an unrecoverable hang. The Intel Lunar Lake display/GPU does not
cleanly transition to a power-down state when s2idle is entered while the display
pipeline is active.

## Solution

A systemd service that blanks the display before `systemd-suspend.service` runs.
It locks all sessions (causing GNOME to blank the display via proper DPMS) and waits
5 seconds for the display hardware to fully power down.

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
# Display-blanking service (prevents hang on suspend)
sudo cp surface-pre-suspend.sh /usr/local/bin/
sudo chmod +x /usr/local/bin/surface-pre-suspend.sh
sudo cp surface-display-off.service /etc/systemd/system/

# Resume inhibitor (prevents immediate re-suspend from lid switch bounce)
sudo cp surface-resume-inhibit.service /etc/systemd/system/

# Make lid switch respect the inhibitor
sudo mkdir -p /etc/systemd/logind.conf.d
sudo cp logind-surface-lid-debounce.conf /etc/systemd/logind.conf.d/

sudo systemctl daemon-reload
sudo systemctl enable surface-display-off.service surface-resume-inhibit.service
sudo systemctl restart systemd-logind
```

## Kernel patch

The lid wake GPE (0x2E) patch should also be applied — see
`patches/6.17/0022-surface-pro-11-intel-lid-wake.patch`.

## Wake sources

- **Power button**: works by default (direct GPIO)
- **Lid open**: works (requires GPE 0x2E patch + `surface_gpe` module)
- **Keyboard**: does not wake (Type Cover uses SAM/UART; enabling SAM wakeup causes spurious self-wakes)
