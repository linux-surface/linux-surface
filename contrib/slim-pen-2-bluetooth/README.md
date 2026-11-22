# Surface Slim Pen 2 Bluetooth Auto-Reconnect Configuration

This directory provides a minimal BlueZ configuration snippet to fix Surface Slim Pen 2 BLE auto-reconnection on Linux (see [linux-surface/linux-surface#1656](https://github.com/linux-surface/linux-surface/issues/1656)).

## Root Cause

With BlueZ's default `Privacy = off`, the host never distributes a Local IRK (`kdist_peer.id = 0`) during BLE SMP pairing. The Surface Slim Pen 2 firmware stores the bond without a valid privacy-capable flag and subsequently refuses to transmit BLE reconnect advertisements (`ADV_IND`) when picked up, lifted from the charger, short-clicked, or woken after suspend — forcing users to hold the top button for 5–7 seconds to re-enter pairing mode every time.

Enabling `Privacy = device` matches Windows behavior: BlueZ generates a persistent Local IRK, enables controller privacy (`privacy` and `ll-privacy`), and distributes the IRK during SMP pairing. Once re-paired, the pen reconnects automatically on movement/lift, a single short top-button click, resume from suspend, and reboot without background discovery polling.

Tested on both ARM64 (Surface Pro X SQ2, Arch Linux, BlueZ 5.87) and Intel (Surface Pro 11, Fedora 44, BlueZ 5.87).

## Installation

Do **not** blindly replace `/etc/bluetooth/main.conf`; instead, merge the settings from [`main.conf`](main.conf) into your existing `/etc/bluetooth/main.conf` under the `[General]` (and optionally `[LE]`) sections:

```ini
[General]
Privacy = device
FastConnectable = true
```

Then restart BlueZ and **re-pair the Surface Slim Pen 2 once** (an existing bond created while `Privacy = off` does not have the Local IRK):

1. Restart Bluetooth:
   ```sh
   sudo systemctl restart bluetooth
   ```
2. Verify that both `privacy` and `ll-privacy` appear under `current settings`:
   ```sh
   btmgmt info
   ```
3. Remove the old Slim Pen 2 pairing in your Bluetooth settings (or `bluetoothctl remove <PEN_MAC>`).
4. Hold the pen's top button for 5–7 seconds until the LED blinks white and pair it again.

After this one-time re-pair, the pen will reconnect automatically whenever picked up, lifted from the charger, or short-clicked.
