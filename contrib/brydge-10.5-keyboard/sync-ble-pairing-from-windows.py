#!/usr/bin/env python3
"""
Sync a single BLE device pairing from a Windows .reg export into BlueZ.

Use case: dual-boot Windows/Linux with a BLE peripheral (keyboard, mouse,
headset) that was (re)paired on Windows and is now refusing to connect on
Linux because the stored LTK/EDiv/Rand are stale.

This script does NOT mount the Windows registry or decide which pairing
to sync — you do that yourself with `reged -x` and this script's --list
output. It only reads a .reg file you produced and writes a BlueZ-format
info file.

Prerequisites
-------------
* Windows partition mounted (ro is fine).
* chntpw/reged installed: `sudo apt install chntpw`.
* Exported BTHPORT keys to a .reg file:
    sudo reged -x /mnt/win/Windows/System32/config/SYSTEM \\
        'HKEY_LOCAL_MACHINE\\SYSTEM' \\
        '\\ControlSet001\\Services\\BTHPORT\\Parameters\\Keys\\<adapter-hex>' \\
        out.reg
  (use chntpw/reged, NOT chntpw -e with heredoc — it will loop forever and
  fill your disk. See
  https://github.com/linux-surface/linux-surface/issues/<whatever>)

Workflow
--------
1. `sudo ./sync-ble-pairing-from-windows.py --list out.reg`
   → prints every LE subkey with its address, IRK, LTK, EDiv/Rand, and
     (critically) the matching BlueZ directory if one exists.
2. Pick the device you want to sync and run:
   `sudo ./sync-ble-pairing-from-windows.py --apply out.reg <hex-subkey> \\
       --adapter-mac AC:67:5D:04:67:D4`
3. Restart bluetoothd: `sudo systemctl restart bluetooth`
4. Power on the device. It should connect within a few seconds.

Byte-order conventions (empirically verified, April 2026)
---------------------------------------------------------
These differ from what you might expect by analogy with Classic Bluetooth
sync tools (e.g. bt-dualboot, which only handles BR/EDR link keys anyway):

* **IRK**: byte-reversed between Windows and Linux. Windows stores it
  in one order; BlueZ's `Key=` hex string in `[IdentityResolvingKey]`
  is the reverse. We verify this by cross-checking that the reversed
  bytes match an existing Linux IRK for the same physical device.
* **LTK**: NOT reversed. Windows `hex:b5,17,c0,...` becomes Linux
  `Key=B517C0...` directly. Reversing the LTK produces
  `HCI Encryption Change Status: Connection Timeout (0x08)` followed
  by peripheral-initiated `SMP Security Request` — i.e. the peripheral
  doesn't recognize the session and asks to re-pair.
* **ERand**: Windows `hex(b):` is a little-endian u64. Convert to
  BlueZ `Rand=<decimal>` via `int.from_bytes(bytes, "little")`.
* **EDIV**: Windows dword → BlueZ decimal, straight conversion.
* **Address**: Windows 8-byte blob is 6 MAC bytes little-endian +
  2 padding bytes. Reverse the 6 to get the big-endian colon-separated
  BlueZ MAC.
* **Authenticated**: `1` for LE Legacy pairing (passkey entry / just-works
  with MITM), `2` for LE Secure Connections. If you're not sure, try `1`
  first — more peripherals are Legacy than SC, and the symptom of a wrong
  choice is always a connection timeout, never corruption.

What this script does NOT handle
--------------------------------
* BR/EDR (Classic Bluetooth) device sync — use bt-dualboot for those.
* Writing from Linux back to Windows (so if you re-pair on Linux, you
  still have to re-pair on Windows). This tool is one-directional:
  Windows → Linux.
* [SlaveLongTermKey] for LE Legacy pairings that split directions.
  Most modern peripherals use a single symmetric LTK even in Legacy;
  if your device needs a slave LTK you'll have to add it by hand.
* Preserving `[ConnectionParameters]`, `[DeviceID]`, service UUIDs,
  etc. beyond what was already in an existing info file. If a Linux
  info file already exists for this device (matched by IRK), we merge
  with it; otherwise we write a minimal info file and let bluetoothd
  fill in the rest on first connect.
"""
import argparse
import os
import re
import sys
from pathlib import Path


# ----- .reg parser -------------------------------------------------------

_HEX_BYTE = re.compile(r"^[0-9a-fA-F]{2}$")


def parse_reg_file(path):
    """Parse a reged -x export into a dict of subkey -> {value: (type, bytes)}.

    We only need two value types:
      dword:XXXXXXXX  -> int
      hex(b):xx,xx... -> bytes (REG_QWORD, little-endian)
      hex:xx,xx...    -> bytes (REG_BINARY, order-preserved)
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    out = {}
    current = None
    value_buffer = None
    pending_line = ""

    def commit_value(name, raw):
        raw = raw.strip()
        if raw.startswith("dword:"):
            out[current][name] = ("dword", int(raw[6:], 16))
        elif raw.startswith("hex(b):"):
            b = bytes(int(x, 16) for x in raw[7:].split(",") if _HEX_BYTE.match(x))
            out[current][name] = ("hexb", b)
        elif raw.startswith("hex:"):
            b = bytes(int(x, 16) for x in raw[4:].split(",") if _HEX_BYTE.match(x))
            out[current][name] = ("hex", b)
        else:
            out[current][name] = ("raw", raw)

    pending_name = None
    pending_raw = ""
    for raw_line in text.splitlines():
        line = raw_line.rstrip("\\")
        if pending_name is not None:
            pending_raw += line.strip()
            if not raw_line.rstrip().endswith("\\"):
                commit_value(pending_name, pending_raw)
                pending_name = None
                pending_raw = ""
            continue

        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("[") and stripped.endswith("]"):
            current = stripped[1:-1]
            out.setdefault(current, {})
            continue
        if current is None:
            continue
        if "=" not in stripped:
            continue
        name, _, rest = stripped.partition("=")
        name = name.strip().strip('"')
        if raw_line.rstrip().endswith("\\"):
            pending_name = name
            pending_raw = rest.strip()
        else:
            commit_value(name, rest)

    return out


# ----- Windows → BlueZ field conversions ---------------------------------

def to_linux_irk(win_bytes: bytes) -> str:
    return win_bytes[::-1].hex().upper()


def to_linux_ltk(win_bytes: bytes) -> str:
    # Intentionally NOT reversed — see module docstring.
    return win_bytes.hex().upper()


def to_linux_rand(win_qword_le_bytes: bytes) -> int:
    return int.from_bytes(win_qword_le_bytes, "little")


def to_linux_mac(win_address_bytes: bytes) -> str:
    return ":".join(f"{b:02X}" for b in win_address_bytes[:6][::-1])


def win_authreq_uses_sc(authreq_dword: int) -> bool:
    """DEPRECATED: bit 3 of the Windows-stored AuthReq octet indicates the
    central's *capability* (LE Secure Connections supported), not the
    negotiated mode. A peripheral that only supports Legacy will respond
    with its own AuthReq (SC bit clear) and the session ends up Legacy
    even though Windows stores 0x2d. We keep this function for reference
    but we NO LONGER use it to auto-select Authenticated — see
    DEFAULT_AUTHENTICATED below.
    """
    return bool(authreq_dword & 0x08)


# Default [LongTermKey] Authenticated= for LE Legacy pairing with MITM
# (passkey entry). This is the common case for Bluetooth HID peripherals
# (keyboards, mice) with physical number pads for typing a pairing code.
# Override with --force-sc if your device actually negotiates LESC.
DEFAULT_AUTHENTICATED = 1


# ----- LE subkey extraction ----------------------------------------------

LE_KEY_FIELDS = ("LTK", "KeyLength", "ERand", "EDIV", "IRK", "Address", "AddressType", "AuthReq")


def iter_le_subkeys(reg, adapter_hex):
    prefix = f"HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\BTHPORT\\Parameters\\Keys\\{adapter_hex}\\"
    for section in reg:
        if not section.startswith(prefix):
            continue
        subkey = section[len(prefix):]
        # Subkey names are 12-hex-char device MACs.
        if not re.match(r"^[0-9a-f]{12}$", subkey):
            continue
        values = reg[section]
        # Must have at least LTK to be a useful LE pairing.
        if "LTK" not in values:
            continue
        entry = {"subkey": subkey, "section": section}
        for f in LE_KEY_FIELDS:
            entry[f] = values.get(f)
        yield entry


def guess_adapter_hex(reg):
    for section in reg:
        m = re.search(
            r"ControlSet001\\Services\\BTHPORT\\Parameters\\Keys\\([0-9a-f]{12})$",
            section,
        )
        if m:
            return m.group(1)
    return None


# ----- BlueZ info file writing -------------------------------------------

def load_existing_info(info_path: Path):
    """Return a simple {section: {key: value}} dict from an existing info
    file, or {} if it doesn't exist."""
    if not info_path.exists():
        return {}
    out = {}
    current = None
    for raw in info_path.read_text().splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            out.setdefault(current, {})
            continue
        if "=" in line and current is not None:
            k, _, v = line.partition("=")
            out[current][k.strip()] = v.strip()
    return out


def render_info(merged):
    lines = []
    for section, kv in merged.items():
        lines.append(f"[{section}]")
        for k, v in kv.items():
            lines.append(f"{k}={v}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def build_info(entry, existing, authenticated):
    merged = {
        section: dict(kv) for section, kv in existing.items()
    }

    general = merged.setdefault("General", {})
    general.setdefault("Name", "Unknown BLE Device")
    general["AddressType"] = "static"  # addr-type 1 = random static
    general.setdefault("SupportedTechnologies", "LE;")
    general.setdefault("Trusted", "true")
    general.setdefault("Blocked", "false")
    general.setdefault("WakeAllowed", "true")

    irk_bytes = entry["IRK"][1] if entry["IRK"] else None
    ltk_bytes = entry["LTK"][1]
    rand_bytes = entry["ERand"][1] if entry["ERand"] else None
    ediv_int = entry["EDIV"][1] if entry["EDIV"] else 0

    if irk_bytes:
        merged["IdentityResolvingKey"] = {"Key": to_linux_irk(irk_bytes)}

    merged["LongTermKey"] = {
        "Key": to_linux_ltk(ltk_bytes),
        "Authenticated": str(authenticated),
        "EncSize": "16",
        "EDiv": str(ediv_int),
        "Rand": str(to_linux_rand(rand_bytes) if rand_bytes else 0),
    }

    # Drop any stale [SlaveLongTermKey] left over from a previous Legacy
    # pairing — modern peripherals use symmetric LTK.
    merged.pop("SlaveLongTermKey", None)
    return merged


# ----- CLI ----------------------------------------------------------------

def cmd_list(args):
    reg = parse_reg_file(args.reg_file)
    adapter_hex = args.adapter_hex or guess_adapter_hex(reg)
    if not adapter_hex:
        print("Could not determine adapter hex from .reg. Pass --adapter-hex.",
              file=sys.stderr)
        return 1

    print(f"Adapter (reg hex): {adapter_hex}")
    print(f"Adapter MAC:       {':'.join(adapter_hex[i:i+2].upper() for i in range(0, 12, 2))}")
    print()

    found = 0
    for entry in iter_le_subkeys(reg, adapter_hex):
        found += 1
        mac = to_linux_mac(entry["Address"][1]) if entry["Address"] else "(no Address value)"
        irk = to_linux_irk(entry["IRK"][1]) if entry["IRK"] else "(no IRK — not resolvable)"
        ediv = entry["EDIV"][1] if entry["EDIV"] else 0
        authreq = entry["AuthReq"][1] if entry["AuthReq"] else 0
        sc_capable = win_authreq_uses_sc(authreq)
        print(f"  Subkey:        {entry['subkey']}")
        print(f"  Address:       {mac}")
        print(f"  AuthReq:       0x{authreq:02x} (central capability — NOT negotiated mode)")
        print(f"                 SC bit {'set' if sc_capable else 'clear'}")
        print(f"  EDIV:          {ediv}")
        print(f"  IRK (Linux):   {irk}")
        print(f"  LTK (Linux):   {to_linux_ltk(entry['LTK'][1])}")
        print()
    if found == 0:
        print("No LE device subkeys under this adapter.")
        return 1
    print("NOTE: the 'apply' subcommand will default to Authenticated=1 (LE")
    print("Legacy) for the [LongTermKey] section. If encryption fails after")
    print("applying, try again with --force-sc to use Authenticated=2 instead.")
    return 0


def cmd_apply(args):
    reg = parse_reg_file(args.reg_file)
    adapter_hex = args.adapter_hex or guess_adapter_hex(reg)
    if not adapter_hex:
        print("Could not determine adapter hex. Pass --adapter-hex.", file=sys.stderr)
        return 1

    target = None
    for entry in iter_le_subkeys(reg, adapter_hex):
        if entry["subkey"].lower() == args.subkey.lower():
            target = entry
            break
    if target is None:
        print(f"No LE device subkey {args.subkey} under adapter {adapter_hex}",
              file=sys.stderr)
        return 1

    if not target["Address"]:
        print("Target has no Address value; cannot form a BlueZ directory name.",
              file=sys.stderr)
        return 1

    mac = to_linux_mac(target["Address"][1])
    if args.force_sc:
        authenticated = 2
    else:
        # Default to Legacy. See the DEFAULT_AUTHENTICATED docstring for why
        # we don't trust the Windows AuthReq SC bit for auto-detection.
        authenticated = DEFAULT_AUTHENTICATED

    adapter_mac = args.adapter_mac
    if not adapter_mac:
        adapter_mac = ":".join(adapter_hex[i:i+2].upper() for i in range(0, 12, 2))

    dir_path = Path(f"/var/lib/bluetooth/{adapter_mac}/{mac}")
    info_path = dir_path / "info"

    if os.geteuid() != 0:
        print(f"Need root to write to {info_path}", file=sys.stderr)
        return 2

    existing = load_existing_info(info_path)
    merged = build_info(target, existing, authenticated)

    dir_path.mkdir(parents=True, exist_ok=True)
    os.chmod(dir_path, 0o700)
    info_path.write_text(render_info(merged))
    os.chmod(info_path, 0o600)

    print(f"Wrote {info_path}")
    print(f"  LTK:           {merged['LongTermKey']['Key']}")
    print(f"  IRK:           {merged.get('IdentityResolvingKey', {}).get('Key', '(none)')}")
    print(f"  EDiv:          {merged['LongTermKey']['EDiv']}")
    print(f"  Rand:          {merged['LongTermKey']['Rand']}")
    print(f"  Authenticated: {merged['LongTermKey']['Authenticated']}"
          f" ({'LESC' if authenticated == 2 else 'Legacy'})")
    print()
    print("Next steps:")
    print("  sudo systemctl restart bluetooth")
    print("  # power on the device; it should auto-connect within a few seconds")
    print("  # verify: bluetoothctl info", mac)
    print()
    print("If connection fails with 'Connection Timeout' in btmon:")
    print("  - Try --force-legacy or --force-sc to flip the Authenticated value")
    print("  - If that doesn't help, check the btmon trace for the actual LTK")
    print("    the kernel is sending and compare to the value above")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Sync a BLE device pairing from a Windows .reg export into BlueZ",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_list = sub.add_parser("list", help="show LE devices in the .reg export")
    p_list.add_argument("reg_file", help="output of `reged -x ... SYSTEM ...`")
    p_list.add_argument("--adapter-hex", help="adapter MAC as lowercase hex (no colons)")
    p_list.set_defaults(func=cmd_list)

    p_apply = sub.add_parser("apply", help="write a BlueZ info file for a specific LE device")
    p_apply.add_argument("reg_file", help="output of `reged -x ... SYSTEM ...`")
    p_apply.add_argument("subkey", help="device subkey (12 lowercase hex chars)")
    p_apply.add_argument("--adapter-hex", help="adapter MAC as lowercase hex (no colons)")
    p_apply.add_argument("--adapter-mac", help="adapter MAC in BlueZ format (e.g. AC:67:5D:04:67:D4)")
    p_apply.add_argument("--force-legacy", action="store_true",
                          help="set Authenticated=1 regardless of Windows AuthReq SC bit")
    p_apply.add_argument("--force-sc", action="store_true",
                          help="set Authenticated=2 regardless of Windows AuthReq SC bit")
    p_apply.set_defaults(func=cmd_apply)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
