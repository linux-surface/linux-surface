# Surface Pro 11 (Intel) Suspend Fix

Three independent firmware/driver issues conspire to break suspend on the
Surface Pro 11 (Intel Lunar Lake). All three are mitigated by this contrib
package.

## Problems & fixes

### 1. Display-pipeline-active hang on s2idle entry

Entering s2idle while the display pipeline is still active causes an
unrecoverable hang. The Xe display engine does not cleanly transition to a
power-down state in this case.

**Fix:** `surface-display-off.service` blanks the display before
`systemd-suspend.service` runs. It locks all sessions (so GNOME blanks via
proper DPMS) and waits ~5 s for the display hardware to fully power down.
This must be a systemd service (not a sleep hook) because sleep hooks run
after `user.slice` is frozen, at which point GNOME cannot process the DPMS
request.

### 2. GPE 0xAB — TRP3 hot-plug GPE on a port that doesn't exist

GPE 0xAB is the PCIe hot-plug interrupt for Thunderbolt Root Port #3 (PCI
00:07.3). On the Surface Pro 11 only TRP0 (00:07.0) and TRP1 (00:07.1) are
physically present — TRP3 doesn't exist on this SKU. The firmware's `_LAB`
handler calls `SLAB()`, which is declared External in the DSDT but never
defined in any SSDT (Microsoft BIOS bug, confirmed by ACPI table
disassembly). The GPE fires spuriously, `_LAB` runs, the lookup fails, and
the kernel logs `ACPI BIOS Error (bug): Could not resolve symbol
[\_GPE._LAB.SLAB], AE_NOT_FOUND`.

The error flood saturates `dmesg` / journald, contributes to the
second-cycle suspend hangs (printk contention during `dpm_suspend`), and on
some kernels also prevents the machine from shutting down cleanly.

**Fix:** three layers, because no single one is fully persistent across
s2idle:
- Kernel cmdline `acpi_mask_gpe=0xAB` — early-boot kernel-side mask.
- `surface-disable-gpe-ab.service` — boot-time oneshot that writes
  `disable` to `/sys/firmware/acpi/interrupts/gpeAB` once sysfs is up,
  disabling the GPE in the chipset register.
- `52-gpe-ab-disable` system-sleep hook — re-applies the chipset disable
  on every resume.

Real Thunderbolt hot-plug uses pciehp/USB4 NHI native signalling, not ACPI
GPEs, so disabling 0xAB has no functional impact on the ports that do
exist.

### 3. PCIe ASPM policy choice

`pcie_aspm=force` is required to match the Windows `ASPM=MaxSaving`
baseline. But `pcie_aspm.policy=powersupersave` is **too aggressive**:
empirically it drives the TB4 root ports into L1.2 during s2idle and they
fail to exit, wedging the xhci/NHI controllers (USB-C devices die after
the first suspend until reboot, and second-cycle suspends hang).

**Fix:** `pcie_aspm.policy=default` — gets ASPM enabled (because of
`pcie_aspm=force`) without the L1.2-trap. Suspend stays stable across
many cycles.

## Required kernel parameters

Add to GRUB (`/etc/default/grub`):
```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash acpi_sleep=nonvs acpi_osi=\"Windows 2022\" button.lid_init_state=open pcie_aspm=force pcie_aspm.policy=default acpi_mask_gpe=0xAB"
```

Then run `sudo update-grub`.

- `acpi_sleep=nonvs` — prevents hang during s2idle entry.
- `acpi_osi="Windows 2022"` — lets ACPI firmware advertise modern-standby
  / S0ix codepaths. `"Windows 2020"` also works; `2022` advertises the
  most recent OSI string the firmware recognises.
- `button.lid_init_state=open` — fixes display not turning on after power
  button wake. The ACPI `_LID` method returns stale "closed" state after
  s2idle because the GPU's cached lid state (`GFX0.CLID`) is not updated
  during suspend; without this, GNOME thinks the lid is closed and won't
  activate the display.
- `pcie_aspm=force pcie_aspm.policy=default` — enables ASPM matching
  Windows behaviour, without the `powersupersave` L1.2 trap (see issue 3
  above).
- `acpi_mask_gpe=0xAB` — masks the spurious TRP3 hot-plug GPE early at
  boot. Belt-and-suspenders for `surface-disable-gpe-ab.service`, which
  applies the stronger chipset-register disable once sysfs is up.

## Installation

```bash
sudo ./install.sh          # install all components
sudo ./install.sh --dry-run # preview what would be done
sudo ./install.sh --remove  # uninstall
```

Or manually:

```bash
# Display-blanking service (prevents hang on suspend — issue 1)
sudo cp surface-pre-suspend.sh /usr/local/bin/
sudo chmod +x /usr/local/bin/surface-pre-suspend.sh
sudo cp surface-display-off.service /etc/systemd/system/

# GPE 0xAB disable (issue 2): boot-time service + post-resume hook
sudo cp surface-disable-gpe-ab.service /etc/systemd/system/
sudo cp 52-gpe-ab-disable /usr/lib/systemd/system-sleep/
sudo chmod +x /usr/lib/systemd/system-sleep/52-gpe-ab-disable

# Resume inhibitor (prevents immediate re-suspend from lid switch bounce)
sudo cp surface-resume-inhibit.service /etc/systemd/system/

# Make lid switch respect the inhibitor
sudo mkdir -p /etc/systemd/logind.conf.d
sudo cp logind-surface-lid-debounce.conf /etc/systemd/logind.conf.d/

sudo systemctl daemon-reload
sudo systemctl enable --now surface-disable-gpe-ab.service
sudo systemctl enable surface-display-off.service surface-resume-inhibit.service

# Power tuning: Intel WiFi firmware-level power saving
sudo cp iwlwifi-powersave.conf /etc/modprobe.d/

# Reboot for logind config + GRUB params to take effect
# WARNING: Do NOT run 'systemctl restart systemd-logind' from a graphical
# session — it kills all sessions and crashes GNOME.
```

## Hibernate / Suspend-then-Hibernate

With this fix applied, s2idle on the Surface Pro 11 draws ~200 mW average
(~0.4 %/hr, ~10 days standby) measured across multi-cycle test sessions;
worst-case cycles run ~350 mW. For longer standby, you can enable
**suspend-then-hibernate**: the system suspends with s2idle first, then
automatically hibernates (writes RAM to swap and powers off completely)
after a configurable delay.

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
