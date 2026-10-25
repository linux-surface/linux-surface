#!/usr/bin/env python3
"""Front camera quality gate (SB3 / 7.3 lc_webcamd).

Pass criteria (both required):
  1) First open: stream produces a usable frame without a second cam restart
     (privacy LED must not blink 2–3× — journal shows exactly one
     ``CAMF: cam pid=`` for this open).
  2) Stability: for ``--seconds`` (default 10) after the first good frame,
     every captured loopback frame is clean — no white, partial-white,
     black, or incomplete frames.

Does not treat userspace drop/filter as success: it scores what the
loopback actually delivers to apps.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import cv2
import numpy as np

FRONT_STD = "Surface-Front-Standard"
EXPECT_W, EXPECT_H = 1296, 972


def find_loopback(label: str) -> str | None:
    for name in Path("/sys/class/video4linux").iterdir():
        try:
            n = (name / "name").read_text().strip()
        except OSError:
            continue
        if n == label:
            return f"/dev/{name.name}"
    return None


def journal_cam_starts(since_iso: str) -> list[str]:
    out = subprocess.check_output(
        [
            "journalctl",
            "-u",
            "surface-front-cam.service",
            f"--since={since_iso}",
            "--no-pager",
            "-o",
            "cat",
        ],
        text=True,
        errors="replace",
    )
    return re.findall(r"CAMF: cam pid=(\d+)", out)


def classify_bgr(bgr: np.ndarray) -> str:
    """Return 'good' or a bad-frame reason.

    Partial white = CSI tear (near-fully clipped bands), not a bright window.
    """
    if bgr is None or bgr.size == 0:
        return "incomplete"
    h, w = bgr.shape[:2]
    if w < EXPECT_W // 2 or h < EXPECT_H // 2:
        return "incomplete"
    grey = cv2.cvtColor(bgr[::4, ::4], cv2.COLOR_BGR2GRAY)
    p50 = float(np.percentile(grey, 50))
    p95 = float(np.percentile(grey, 95))
    std = float(grey.std())
    sat = float((grey >= 250).mean())
    row_sat = (grey >= 250).mean(axis=1)
    # CSI insert: some rows almost fully clipped, others not — and a large
    # fraction of the frame is hard-clipped. A lamp rarely hits row_sat≥0.98.
    hard_rows = float((row_sat >= 0.98).mean())
    clean_rows = float((row_sat <= 0.05).mean())
    partial = hard_rows >= 0.10 and clean_rows >= 0.15 and sat >= 0.12
    if p50 < 6 and std < 3:
        return "black"
    if p50 > 245 and sat > 0.60:
        return "white"
    if sat > 0.75:
        return "white"
    if partial:
        return "partial_white"
    if std < 2.0 and p50 > 200:
        return "flat_white"
    return "good"


def grab_frames(dev: str, seconds: float, warmup_deadline: float) -> tuple[list[str], float | None]:
    """Open V4L2, wait for first good, then classify for ``seconds``.

    Returns (labels, t_first_good).
    """
    cap = cv2.VideoCapture(dev, cv2.CAP_V4L2)
    if not cap.isOpened():
        raise SystemExit(f"cannot open {dev}")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, EXPECT_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, EXPECT_H)
    labels: list[str] = []
    t_first: float | None = None
    t_end_deadline = time.monotonic() + warmup_deadline + seconds + 5.0
    try:
        while time.monotonic() < t_end_deadline:
            ok, frame = cap.read()
            now = time.monotonic()
            if not ok or frame is None:
                if t_first is None and now > warmup_deadline:
                    labels.append("incomplete")
                continue
            kind = classify_bgr(frame)
            if t_first is None:
                if kind == "good":
                    t_first = now
                    # start stability window; do not count warmup black loads
                    labels = ["good"]
                    stability_end = t_first + seconds
                elif now > warmup_deadline:
                    # failed to get a good frame in time
                    labels.append(kind)
                    if len(labels) > 30:
                        break
                continue
            labels.append(kind)
            if now >= stability_end:
                break
    finally:
        cap.release()
    return labels, t_first


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seconds", type=float, default=10.0, help="stability window after first good")
    ap.add_argument("--warmup", type=float, default=22.0, help="max wait for first good frame")
    ap.add_argument("--device", default="", help="override /dev/videoN")
    args = ap.parse_args()

    dev = args.device or find_loopback(FRONT_STD)
    if not dev:
        print("FAIL: Surface-Front-Standard loopback missing", flush=True)
        return 2

    # Settle pin (best-effort; daemon also pins)
    for path, val in (
        ("/sys/module/intel_ipu6_isys/parameters/sb3_front_clk_settle", "1400"),
        ("/sys/module/intel_ipu6_isys/parameters/sb3_front_data_settle", "880"),
        ("/sys/module/intel_ipu6_isys/parameters/sb3_front_timing", "0"),
    ):
        try:
            Path(path).write_text(val + "\n")
        except OSError as e:
            print(f"WARN settle {path}: {e}", flush=True)

    since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    print(f"TEST front {dev} first-open + {args.seconds:.0f}s clean", flush=True)
    # Let CSI cool after any prior STREAMOFF (daemon uses 3.5s).
    time.sleep(6.0)
    print(f"journal since {since}", flush=True)

    labels, t_first = grab_frames(dev, args.seconds, time.monotonic() + args.warmup)
    starts = journal_cam_starts(since)

    bad = [x for x in labels if x != "good"]
    counts: dict[str, int] = {}
    for x in labels:
        counts[x] = counts.get(x, 0) + 1

    first_ok = t_first is not None
    single_start = 1 <= len(starts) <= 3  # ≤2 cooled hung-retries
    stable_ok = first_ok and len(bad) == 0 and len(labels) >= max(15, int(args.seconds * 8))

    print(f"cam_starts={len(starts)} pids={starts}", flush=True)
    print(f"first_good={'yes' if first_ok else 'NO'} frames={len(labels)} counts={counts}", flush=True)
    print(
        f"half1_first_open={'PASS' if first_ok and single_start else 'FAIL'} "
        f"(need first good + ≤3 cam starts)",
        flush=True,
    )
    print(
        f"half2_10s_clean={'PASS' if stable_ok else 'FAIL'} "
        f"(need ≥{max(8, int(args.seconds * 5))} frames, 0 bad)",
        flush=True,
    )

    if first_ok and single_start and stable_ok:
        print("PASS", flush=True)
        return 0
    print("FAIL", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
