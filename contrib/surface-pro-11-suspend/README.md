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
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash acpi_sleep=nonvs acpi_osi=\"Windows 2020\" button.lid_init_state=open pcie_aspm=force pcie_aspm.policy=powersupersave"
```

Then run `sudo update-grub`.

- `acpi_sleep=nonvs` — prevents hang during s2idle entry
- `acpi_osi="Windows 2020"` — enables S0ix support in ACPI firmware
- `button.lid_init_state=open` — fixes display not turning on after power button wake.
  The ACPI `_LID` method returns stale "closed" state after s2idle because the GPU's
  cached lid state (`GFX0.CLID`) is not updated during suspend. This causes GNOME to
  think the lid is closed and not activate the display. Setting `lid_init_state=open`
  forces the lid to report as open on every resume.
- `pcie_aspm=force pcie_aspm.policy=powersupersave` — enables PCIe ASPM L1.2 substates
  for maximum power savings. Matches Windows `ASPM=MaxSaving` policy which is set across
  all power schemes (including High Performance) on the Surface Pro 11.

## Installation

```bash
sudo ./install.sh          # install all components
sudo ./install.sh --dry-run # preview what would be done
sudo ./install.sh --remove  # uninstall
```

Or manually:

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

# Power tuning: Intel WiFi firmware-level power saving
sudo cp iwlwifi-powersave.conf /etc/modprobe.d/

# Reboot for logind config to take effect
# WARNING: Do NOT run 'systemctl restart systemd-logind' from a graphical
# session — it kills all sessions and crashes GNOME.
```

## Hibernate / Suspend-then-Hibernate

By default, the Surface Pro 11 uses s2idle (suspend-to-idle) which draws ~351 mW
(~0.8%/hr, ~5 days standby). For longer standby, you can enable **suspend-then-hibernate**:
the system suspends with s2idle first, then automatically hibernates (writes RAM to swap
and powers off completely) after a configurable delay.

### Setup

```bash
# Interactive setup (auto-detects swap, prompts for delay)
sudo ./surface-hibernate-setup.sh

# Or specify options directly
sudo ./surface-hibernate-setup.sh --delay=1h

# Use a specific swap device
sudo ./surface-hibernate-setup.sh --swap=/dev/sda2 --delay=2h
```

A reboot is required after setup for the GRUB/initramfs changes to take effect.

### What it configures

- **GRUB**: Adds `resume=UUID=...` (and `resume_offset=...` for swap files) to kernel command line
- **initramfs**: Updated to include resume support
- **sleep.conf**: Sets `HibernateDelaySec` (time in s2idle before hibernating)
- **logind.conf**: Changes lid switch action to `suspend-then-hibernate`

### Testing

After reboot:
```bash
# Test hibernate directly
sudo systemctl hibernate

# Test suspend-then-hibernate
sudo systemctl suspend-then-hibernate
```

### Uninstall

```bash
sudo ./surface-hibernate-setup.sh --uninstall
```

This removes all configuration files, updates GRUB/initramfs, and restores default behavior.

## Kernel patch

The lid wake GPE (0x2E) patch should also be applied — see
`patches/6.17/0022-surface-pro-11-intel-lid-wake.patch`.

## Wake sources

- **Power button**: works by default (direct GPIO)
- **Lid open**: works (requires GPE 0x2E patch + `surface_gpe` module)
- **Keyboard**: does not wake (Type Cover uses SAM/UART; enabling SAM wakeup causes spurious self-wakes)
