# Modern Suspend for Surface Pro 5 (2017)

By default, many distros give the Pro 5 a plain suspend that drains the
battery overnight, sometimes loses Wi-Fi on resume, and never hibernates.
This setup uses newer systemd sleep features instead.

| Feature | What it does |
|---|---|
| `MemorySleepMode=deep s2idle` (systemd 256+) | Uses S3 "deep" sleep if the firmware offers it, otherwise s2idle (S0ix, the "Modern Standby" Windows uses on this device). On older systemd, the installer sets the same thing with a tmpfiles rule |
| `suspend-then-hibernate` with battery estimation (systemd 252+) | Closing the Type Cover suspends. If the battery gets low, or after 3 h, the device hibernates to disk, so it doesn't die in your bag |
| Wi-Fi sleep hook | Unloads the Marvell `mwifiex_pcie` driver before suspend and reloads it after resume. This prevents "Wi-Fi gone after resume" and lets s2idle reach its deepest state |
| `--test` | Suspends for 60 s and reports whether the CPU reached S0ix (the deepest low-power idle state) |

## About "deep" vs s2idle on the Pro 5

The Pro 5 was designed for Modern Standby (s2idle/S0ix). If
`cat /sys/power/mem_sleep` shows only `[s2idle]`, the firmware does not offer
S3, and Linux can't add it. The newest suspend the hardware supports is then
s2idle that actually reaches S0ix, plus hibernation as a backstop. This setup
provides exactly that. If your firmware does list `deep`, the setup uses it
automatically.

## Files

| File | Installed to |
|---|---|
| `sleep.conf.d/50-surface-pro-5.conf` | `/etc/systemd/sleep.conf.d/` |
| `logind.conf.d/50-surface-pro-5.conf` | `/etc/systemd/logind.conf.d/` (the installer switches it to plain `suspend` if hibernation can't work) |
| `system-sleep/surface-pro-5-sleep` | `/usr/lib/systemd/system-sleep/` |
| — | `/etc/surface-pro-5-suspend.conf`: set `UNLOAD_WIFI=no` to turn off the Wi-Fi hook |

## Usage

```sh
./surface-pro-5-suspend.sh                 # shows what your device supports, changes nothing
sudo ./surface-pro-5-suspend.sh --install  # then reboot
sudo ./surface-pro-5-suspend.sh --test     # verify deep idle is reached
sudo ./surface-pro-5-suspend.sh --uninstall
```

## Getting hibernation working

suspend-then-hibernate is only enabled when hibernation can work. The check
script tells you what's missing:

- **Swap:** a swap partition or swap file on disk, at least about the size
  of your RAM (8 GB on most Pro 5 models). zram does not count.
- **Resume location:** `resume=UUID=<swap uuid>` on the kernel command line.
  With systemd 255+ and a systemd-based initramfs (dracut, which EndeavourOS
  uses, or mkinitcpio with the `systemd` hook), this is set up automatically.
- **Secure Boot / lockdown:** kernel lockdown blocks hibernation. Disable
  Secure Boot in the Surface UEFI, or accept plain suspend.

After fixing these, run `--install` again.

The Wi-Fi hook works best with the linux-surface kernel, which also contains
mwifiex fixes for Surface devices (`patches/*/0003-mwifiex.patch`).
