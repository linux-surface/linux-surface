#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate a synthetic surface-touchd recording that looks like iptsd output
on a Surface Pro 5 (axis ranges and resolution from iptsd's SP5 preset)."""

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
SYN_REPORT = 0
ABS_X, ABS_Y, ABS_PRESSURE, ABS_TILT_X, ABS_TILT_Y = 0x00, 0x01, 0x18, 0x1a, 0x1b
ABS_MT_SLOT, ABS_MT_TOUCH_MAJOR, ABS_MT_TOUCH_MINOR, ABS_MT_ORIENTATION = 0x2f, 0x30, 0x31, 0x34
ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x35, 0x36, 0x39
BTN_TOUCH, BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_STYLUS = 0x14a, 0x140, 0x141, 0x14b

out = ["# synthetic Surface Pro 5 recording (iptsd ranges)",
       f"touch_abs {ABS_MT_POSITION_X} 0 9600 37", f"touch_abs {ABS_MT_POSITION_Y} 0 7200 42",
       f"touch_abs {ABS_MT_TOUCH_MAJOR} 0 12000 46", f"touch_abs {ABS_MT_TOUCH_MINOR} 0 12000 46",
       f"touch_abs {ABS_MT_ORIENTATION} 0 180 0",
       f"pen_abs {ABS_X} 0 9600 37", f"pen_abs {ABS_Y} 0 7200 42", f"pen_abs {ABS_PRESSURE} 0 4096 0",
       f"pen_abs {ABS_TILT_X} -9000 9000 5730", f"pen_abs {ABS_TILT_Y} -9000 9000 5730"]
t = 1000
tid = 1

def ev(dev, typ, code, val):
    out.append(f"{dev} {t} {typ} {code} {val}")

def frame(fingers):
    """fingers: list of (slot, id or -1, x 0..1, y 0..1)"""
    for slot, i, x, y in fingers:
        ev("T", EV_ABS, ABS_MT_SLOT, slot)
        ev("T", EV_ABS, ABS_MT_TRACKING_ID, i)
        if i >= 0:
            ev("T", EV_ABS, ABS_MT_POSITION_X, round(x * 9600))
            ev("T", EV_ABS, ABS_MT_POSITION_Y, round(y * 7200))
            ev("T", EV_ABS, ABS_MT_TOUCH_MAJOR, 200)
            ev("T", EV_ABS, ABS_MT_TOUCH_MINOR, 160)
            ev("T", EV_ABS, ABS_MT_ORIENTATION, 90)
    ev("T", EV_SYN, SYN_REPORT, 0)

def gesture(n, start, end, ms, steps=10, spread=0.06):
    global t, tid
    ids = list(range(tid, tid + n))
    tid += n
    for s in range(steps + 1):
        f = s / steps
        frame([(k, ids[k], start[0] + (end[0] - start[0]) * f + k * spread,
                start[1] + (end[1] - start[1]) * f) for k in range(n)])
        t += ms // steps
    t += 8
    frame([(k, -1, 0, 0) for k in range(n)])
    t += 400

# 3-finger swipe left
gesture(3, (0.6, 0.5), (0.3, 0.5), 300)
# 2-finger tap
gesture(2, (0.4, 0.4), (0.4, 0.4), 100, steps=2)
# 4-finger swipe up
gesture(4, (0.3, 0.7), (0.3, 0.35), 300)
# edge swipe from the left edge
gesture(1, (0.004, 0.5), (0.25, 0.5), 250)
# 2-finger pinch out (fingers move apart symmetrically)
ids = [tid, tid + 1]
tid += 2
for s in range(11):
    d = 0.04 + 0.012 * s
    frame([(0, ids[0], 0.5 - d / 1.5, 0.5), (1, ids[1], 0.5 + d / 1.5, 0.5)])
    t += 20
frame([(0, -1, 0, 0), (1, -1, 0, 0)])
t += 400
# 1-finger long press (held 800 ms)
for s in range(9):
    frame([(0, tid, 0.5, 0.5)])
    t += 100
frame([(0, -1, 0, 0)])
tid += 1
t += 400
# pen: hover in, touch with pressure, lift, leave
for i, (prox, touch, p) in enumerate([(1, 0, 0), (1, 1, 1024), (1, 1, 3072), (1, 0, 0), (0, 0, 0)]):
    ev("P", EV_KEY, BTN_TOOL_PEN, prox)
    ev("P", EV_KEY, BTN_TOUCH, touch)
    ev("P", EV_ABS, ABS_X, 4800 + i * 10)
    ev("P", EV_ABS, ABS_Y, 3600)
    ev("P", EV_ABS, ABS_PRESSURE, p)
    ev("P", EV_ABS, ABS_TILT_X, 2865)  # 0.5 rad in units of 5730/rad -> ~28.6 deg
    ev("P", EV_SYN, SYN_REPORT, 0)
    t += 15

print("\n".join(out))
