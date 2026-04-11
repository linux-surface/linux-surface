# Brydge 10.5 Go+ keyboard on Linux

A full recipe for getting the **Brydge 10.5 Go+** keyboard — the detachable
keyboard/trackpad designed specifically for the Microsoft Surface Go / Go 2
— working end-to-end under Linux, including:

1. **Trackpad regression workaround**: install-and-pin BlueZ 5.62 on Ubuntu
   22.04, because BlueZ ≥ 5.64 breaks the trackpad on this keyboard's
   composite HID descriptor.
2. **Initial BLE pairing**: the pairing workflow (passkey entry, LE Legacy
   with MITM) — standard, but worth documenting because the failure modes
   are opaque if you've never done LE Legacy before.
3. **Dual-boot key sync (Windows → Linux)**: a Python helper that reads
   a Windows registry `.reg` export and writes a matching BlueZ pairing
   file, so you don't have to re-pair the keyboard every time you boot
   the other OS. Includes the empirically-verified byte-order conventions
   for LE keys, which differ from Classic Bluetooth.

Tested configuration: **Surface Go 2** running **Ubuntu 22.04 LTS** with
the **`linux-surface`** kernel (verified on 6.18.7) and a **Brydge 10.5
Go+** keyboard (ACPI/USB VID `03F6`, PID `A001`, Iton Technology Corp
company ID `1014`, part of the Brydge Pro / Go / Go+ product line).

## The keyboard

The Brydge 10.5 Go+ is a Bluetooth Low Energy HID-over-GATT peripheral
presenting itself as a composite device:

- **Keyboard** (input): standard HID keyboard descriptor
- **Pointer/trackpad** (input): HID mouse + multitouch descriptor
- **Battery service** (BLE GATT 0x180f)
- **HID service** (BLE GATT 0x1812)
- **Device information service** (BLE GATT 0x180a)
- **Nordic Semiconductor DFU** (GATT 0xfe59) — firmware update channel

Pairing uses **LE Legacy pairing with Passkey Entry**: you type a 6-digit
number on the keyboard when prompted. This gives MITM protection, produces
a single symmetric LTK, and stores `Authenticated=1` in BlueZ's info file.

## Problem 1: BlueZ 5.64 breaks the trackpad

On **Ubuntu 22.04** with the stock `bluez` package (5.64), after pairing
successfully, keyboard input works but the **trackpad produces no cursor
motion**. The trackpad's `input` node in `/dev/input/eventN` exists and
is classified correctly by `udev`, but it emits zero events. `evtest` on
the node just shows the connection info and never prints a single `EV_REL`.

The regression is in bluez, not the kernel — the same keyboard on the
same hardware works correctly if you downgrade `bluez` + `libbluetooth3`
+ `bluez-obexd` to **5.62-2** (sourced from
[snapshot.debian.org](https://snapshot.debian.org/package/bluez/5.62-2/)
since Ubuntu 22.04 never shipped 5.62).

### Workaround

Use the included script:

```bash
sudo ./install-bluez-5.62.sh
```

It downloads `bluez_5.62-2_amd64.deb`, `libbluetooth3_5.62-2_amd64.deb`,
and `bluez-obexd_5.62-2_amd64.deb` from
<https://snapshot.debian.org/archive/debian/20211227T152656Z/pool/main/b/bluez/>,
installs them with `--allow-downgrades`, `apt-mark hold`s them so
`apt upgrade` won't silently reinstall 5.64, and restarts `bluetooth.service`.

Verify:

```bash
/usr/libexec/bluetooth/bluetoothd --version
# 5.62
```

**To reverse** (back to stock 5.64):

```bash
sudo apt-mark unhold bluez libbluetooth3 bluez-obexd
sudo apt install --reinstall bluez libbluetooth3 bluez-obexd
```

### Why not just patch bluez upstream

I haven't tracked down the specific commit in bluez 5.63 or 5.64 that
causes the regression. If someone else wants to bisect, a good starting
point is the HID profile code in `profiles/input/hog-lib.c` and
`src/profile.c` — the symptom (HID characteristics read but no input
events delivered) points at the GATT→uhid translation path. A patch
upstream would be better than pinning an old version forever, but
5.62 is the practical fix today.

## Problem 2: Initial pairing

Standard BLE pairing flow. Nothing special, but included for completeness:

```bash
bluetoothctl
# inside bluetoothctl:
power on
agent on
default-agent
scan on
# wait for "NEW Device XX:XX:XX:XX:XX:XX Brydge 10.5 Go+"
pair XX:XX:XX:XX:XX:XX
# the keyboard will prompt you (via a beep / indicator LED) to type a
# 6-digit passkey that bluetoothctl prints on your terminal. Type it on
# the keyboard and press Enter.
trust XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
scan off
```

After this, `Trusted=true` in `/var/lib/bluetooth/<adapter>/<device>/info`
will make the keyboard auto-reconnect on every subsequent boot as long
as it's powered and in range.

Expected output in `dmesg`:

```
input: Brydge 10.5 Go+ Keyboard as /devices/virtual/misc/uhid/.../input/inputN
input: Brydge 10.5 Go+ Mouse as /devices/virtual/misc/uhid/.../input/inputN+1
input: Brydge 10.5 Go+ as /devices/virtual/misc/uhid/.../input/inputN+2  (trackpad)
hid-generic     0005:03F6:A001.XXXX: input,hidraw: BLUETOOTH HID v0.01 Keyboard [Brydge 10.5 Go+] on <adapter-mac>
hid-multitouch  0005:03F6:A001.XXXX: input,hidraw: BLUETOOTH HID v0.01 Device   [Brydge 10.5 Go+] on <adapter-mac>
```

The `hid-multitouch` binding is the trackpad — if you're on stock bluez
5.64, you'll see this line but no cursor movement. That's the regression.

## Problem 3: Dual-boot key sync (Windows → Linux)

If you dual-boot Windows and re-pair the keyboard on Windows (Option A
from most dual-boot-bluetooth tutorials), the keyboard now has a fresh
LTK/EDiv/Rand that Linux doesn't know about — so when you boot back into
Linux, the keyboard connects at the HCI level but encryption fails and
the session disconnects with `Authentication Failure`.

The existing tool for this is [bt-dualboot], but **bt-dualboot only
handles Classic Bluetooth (BR/EDR)** — it reads link keys from the
adapter-level values in the Windows registry and ignores the LE device
subkeys entirely, where our LTK / EDiv / Rand / IRK actually live. See
[bt-dualboot PR #34][pr34] for an upstream attempt to add LE support
that's been sitting unreviewed for over a year.

[bt-dualboot]: https://github.com/x2es/bt-dualboot
[pr34]: https://github.com/x2es/bt-dualboot/pull/34

This contrib includes a one-shot Python helper that does the LE sync
manually:

```bash
# 1. Mount Windows read-only
sudo mkdir -p /mnt/win
sudo mount -t ntfs-3g -o ro /dev/nvmeXnYpZ /mnt/win

# 2. Export the BTHPORT keys subtree to a .reg file (use reged, NOT
#    `chntpw -e` with a heredoc — chntpw's REPL loops on EOF and can
#    fill your disk before you notice)
sudo reged -x /mnt/win/Windows/System32/config/SYSTEM \
    'HKEY_LOCAL_MACHINE\SYSTEM' \
    '\ControlSet001\Services\BTHPORT\Parameters\Keys\<adapter-hex>' \
    out.reg

# 3. List LE devices and pick the one you want to sync
sudo ./sync-ble-pairing-from-windows.py list out.reg

# 4. Apply the chosen subkey to BlueZ
sudo ./sync-ble-pairing-from-windows.py apply out.reg <hex-subkey> \
    --adapter-mac AC:67:5D:04:67:D4

# 5. Restart bluetoothd; the keyboard should auto-connect on next power-up
sudo systemctl restart bluetooth
sudo umount /mnt/win
```

If encryption fails after `restart bluetooth` — you'll see the failure
in `journalctl -u bluetooth -f` as a rapid loop of `Authentication Failure`
and `Disconnect Complete` — retry step 4 with `--force-sc` to use
`Authenticated=2` instead of the default Legacy `1`. Most BLE peripherals
(including the Brydge 10.5 Go+) use Legacy pairing and `Authenticated=1`
is correct, but Apple's Magic Keyboard, Logitech MX Keys, and other
modern devices use LE Secure Connections and need `2`.

### The byte-order conventions (empirically verified)

| Field | Windows registry | BlueZ info file | Reversal? |
|---|---|---|---|
| `IRK` | `hex:fb,1f,...a1` | `[IdentityResolvingKey] Key=A127...FB` | **Yes**, reverse the bytes |
| `LTK` | `hex:b5,17,...c5` | `[LongTermKey] Key=B517...C5` | **No**, keep the Windows byte order |
| `ERand` | `hex(b):67,6e,...3d` (LE u64) | `[LongTermKey] Rand=<decimal>` | Convert LE bytes → u64 → decimal |
| `EDIV` | `dword:000051db` | `[LongTermKey] EDiv=<decimal>` | dword → decimal |
| `Address` | `hex(b):a2,bc,...e4,00,00` | directory-name MAC `E4:20:66:A2:BC:A2` | Reverse the 6 MAC bytes |
| `AddressType` | `dword:00000001` (random) | `[General] AddressType=static` | dword → string |

The IRK reversal is the one most people assume *also* applies to the
LTK — it does not. Reversing the LTK produces `HCI Encryption Change
Status: Connection Timeout (0x08)` followed by the peripheral issuing
an `SMP: Security Request` asking for a fresh pairing, because the key
math doesn't match what the peripheral has stored.

The `sync-ble-pairing-from-windows.py` helper applies these conventions
automatically.

### Diagnosing sync failures with `btmon`

If the sync doesn't work and you want to understand *why*, `btmon` captures
the HCI traffic during a connect attempt:

```bash
sudo btmon -w /tmp/trace.snoop &
BTMON_PID=$!
bluetoothctl connect <mac>
# ... wait ~6 seconds ...
sudo kill -INT $BTMON_PID
sudo btmon -r /tmp/trace.snoop | grep -iE \
    'LE Start Enc|Encryption Change|SMP:|Security Req|Authentication Fail|Disconnect'
```

What to look for:
- **`LE Start Encryption`** (`cmd 0x08|0x0019`) shows the kernel sending
  `Random number`, `Encrypted diversifier`, and `Long term key` — the
  bytes here should match what's in your BlueZ info file. If they don't,
  BlueZ didn't load the info file correctly (syntax error? wrong path?).
- **`Encryption Change Status: Success (0x00)`** → the LTK is correct.
  Done.
- **`Encryption Change Status: Connection Timeout (0x08)`** + peripheral
  `SMP: Security Request` → the LTK math value is wrong. Usually a
  byte-order issue or a stale value from a previous pairing.
- **`Authentication Requirement: ... Legacy, No Keypresses (0x05)`** in
  the peripheral's Security Request → the device does Legacy pairing,
  use `Authenticated=1`.
- **`Authentication Requirement: ... Secure Connections, ... (0x0d)`**
  in the peripheral's Security Request → use `Authenticated=2`.

## Repository contents

```
contrib/brydge-10.5-keyboard/
├── README.md                              ← this file
├── install-bluez-5.62.sh                  ← downgrade + pin bluez for the trackpad
└── sync-ble-pairing-from-windows.py       ← Windows → Linux BLE pairing sync helper
```

## Known limitations

- **Ubuntu 22.04 / Debian-family only.** The install script uses apt and
  pulls Debian .debs from snapshot.debian.org. Fedora / Arch users will
  need to build bluez 5.62 from source — the tarball is at
  <https://www.kernel.org/pub/linux/bluetooth/bluez-5.62.tar.xz>.
- **x86_64 only.** The .deb URLs hard-code `_amd64.deb`. ARM Surface
  users (Pro X, Pro 9 SQ3) need the `_arm64.deb` or `_armel.deb` variants
  from the same snapshot directory.
- **Not tested with LE Secure Connections peripherals.** The sync script
  defaults to `Authenticated=1` because the Brydge is Legacy. The
  `--force-sc` flag exists for LESC devices but hasn't been exercised
  against a real LESC peripheral yet — file an issue if it doesn't work
  for your device.
- **No automatic Windows direction.** If you re-pair on Linux you'll
  need to re-pair on Windows too (the script doesn't write into the
  Windows registry). Adding that direction would mean handling
  byte-order conversion in reverse and modifying Windows-side .reg
  files via `reged -I`, which is doable but out of scope for this
  contrib. See the main linux-surface issue tracker if you want to
  pick it up.
- **`[SlaveLongTermKey]`** isn't handled. A few older LE peripherals
  use split LTKs (one per direction). The Brydge uses a single
  symmetric LTK even in Legacy mode. If your device needs a slave
  LTK, the script won't sync it.

## References

- [linux-surface/linux-surface#1569][1569] — parent tracking issue for
  IPU3 camera tuning on Surface devices (separate from this work, but
  another "Surface peripheral needs userspace workaround" story)
- [bt-dualboot PR #34][pr34] — open attempt to add LE LongTermKey
  support to bt-dualboot, partially solves the problem but doesn't
  cover Windows-side subkey reading, byte-order conventions, or the
  `Authenticated` field
- [BlueZ upstream][bluez] — https://git.kernel.org/pub/scm/bluetooth/bluez.git/
- [SMP spec, Core Spec v5.3 Vol 3 Part H][smp] — the authoritative
  reference for LE pairing AuthReq bits

[1569]: https://github.com/linux-surface/linux-surface/issues/1569
[bluez]: https://git.kernel.org/pub/scm/bluetooth/bluez.git/
[smp]: https://www.bluetooth.com/specifications/specs/core-specification-5-3/
