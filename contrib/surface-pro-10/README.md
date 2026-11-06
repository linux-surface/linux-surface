# Surface Pro 10 (Meteor Lake) - Suspend/Resume Fixes

## Installation

Copy the udev rule for lid-open wake:

    sudo cp 99-surface-pro-10-wakeup.rules /etc/udev/rules.d/
    sudo udevadm control --reload-rules

Install the button fix service:

    sudo cp surface-pro-10-buttons.service /etc/systemd/system/
    sudo systemctl enable --now surface-pro-10-buttons.service

## Kernel Parameters

Add to your kernel command line (e.g. in /etc/default/grub):

    i915.enable_psr2_sel_fetch=0

This fixes s2idle suspend/resume freezes caused by PSR2 selective fetch
failures on the Meteor Lake display pipeline.

## Issues Fixed

- s2idle suspend freeze (system never resumes after lid close)
- Power/volume buttons unreliable after boot (prevents wake)
- Lid open not waking system from suspend
- Kernel panic on resume caused by surface-quickspi hook unloading intel_thc

## Notes

The surface-quickspi sleep hook must NOT be used on Surface Pro 10 as it
causes an xhci kernel panic on resume by unloading intel_thc while USB
is active.
