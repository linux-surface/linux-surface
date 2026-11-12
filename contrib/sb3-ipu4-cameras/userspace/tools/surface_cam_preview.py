#!/usr/bin/env python3
"""Drop-in preview for Surface loopbacks — guvcview SIGSEGVs in yuyv_to_yu12
on v4l2loopback YUYV (Fedora guvcview-2.1). OpenCV path is stable.
"""
from __future__ import annotations

import argparse
import os
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser(description="Surface camera preview (guvcview-safe)")
    ap.add_argument("-d", "--device", default="/dev/video60")
    ap.add_argument("-x", "--resolution", default="", help="WxH (optional)")
    # Accept/ignore guvcview-style flags so callers can swap the binary.
    ap.add_argument("-g", "--gui", default="none")
    ap.add_argument("-a", "--audio", default="none")
    ap.add_argument("extra", nargs="*")
    args, _unknown = ap.parse_known_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for tag in ("vendor-py315", "vendor"):
        p = os.path.join(root, tag)
        if os.path.isdir(p):
            sys.path.insert(0, p)
    sys.path.insert(0, os.path.join(root, "lib"))

    import cv2

    cap = cv2.VideoCapture(args.device, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    if args.resolution and "x" in args.resolution.lower():
        w, h = args.resolution.lower().split("x", 1)
        try:
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, int(w))
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, int(h))
        except Exception:
            pass
    if not cap.isOpened():
        print(f"cannot open {args.device}", file=sys.stderr)
        return 1
    title = f"Surface preview — {args.device}"
    cv2.namedWindow(title, cv2.WINDOW_NORMAL)
    t0 = time.monotonic()
    n = 0
    while True:
        ok, fr = cap.read()
        if not ok:
            time.sleep(0.01)
            continue
        n += 1
        cv2.imshow(title, fr)
        key = cv2.waitKey(1) & 0xFF
        if key in (27, ord("q")):
            break
        if time.monotonic() - t0 > 3600:
            break
    cap.release()
    cv2.destroyAllWindows()
    print(f"frames={n}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
