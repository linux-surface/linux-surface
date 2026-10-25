#!/usr/bin/env python3
"""Guards for the 6.19.8-3 / 7.2.12 camera contract. No hardware required."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DAEMON = ROOT / "userspace" / "tools" / "surface_lc_webcamd.py"
text = DAEMON.read_text()

required = [
    '("Surface-Front-Standard", 55, 1296, 972, 1296, 972, 30)',
    '("Surface-Front-HQ", 56, 2592, 1944, 1920, 1440, 15)',
    '("Surface-Front-Fast", 57, 1296, 972, 1296, 728, 60)',
    '("Surface-Back-Standard", 58, 1632, 1224, 1632, 1224, 30)',
    '("Surface-Back-HQ", 59, 3264, 2448, 1920, 1440, 15)',
    '("Surface-Back-Fast", 60, 800, 600, 800, 450, 60)',
    '_FRONT_SETTLE = (880, 1400)',
    'sb3_front_clk_settle',
    'sb3_front_data_settle',
    "LED off until a client",
    "no client → STREAMOFF",
    "find_cam(",
]
forbidden = [
    "1500/800",
    "_FRONT_SETTLES",

    "1920, 1080",
    "sat > 0.08",
    "cam -c 3",
    "/home/",
    "Pulpit",
    "/run/user/1000",
]
failed = False
for needle in required:
    if needle not in text:
        print("MISSING", needle)
        failed = True
for needle in forbidden:
    if needle in text:
        print("FORBIDDEN", needle)
        failed = True
# IR output is portrait 480x640 from a 640x480 capture.
ir = ROOT / "userspace" / "tools" / "surface_lc_webcamd.py"
ir_text = ir.read_text()
if "role=raw,width=640,height=480" not in ir_text or "(480, 640)" not in ir_text:
    print("MISSING IR 640x480 -> 480x640")
    failed = True
unit = pathlib.Path("/etc/systemd/system/surface-front-cam.service")
if unit.exists() and "Conflicts=" in unit.read_text():
    print("FORBIDDEN Conflicts= in front unit")
    failed = True
sys.exit(1 if failed else 0)
