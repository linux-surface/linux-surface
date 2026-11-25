#!/usr/bin/env python3
"""Stable LIVE Back (ov8865) → /dev/video61.

Causes of drift / flash / "image from the past":
  • v4l2-ctl dumps stride*h-4 (data_offset=4), not Size Image or stride*h
  • queue of raw frames + 8 mmap buffers + ffmpeg pipe
  • WB/percentile computed every frame

Here: package = stride*h-4, newest frame only, YUYV straight to loopback,
      locked WB, mmap=3.
"""
from __future__ import annotations

try:
    from pycompat import MODERN_PY, CV2_V5, bgr_to_yuyv_u8  # noqa: F401
except ImportError:
    MODERN_PY = False
    CV2_V5 = False
    bgr_to_yuyv_u8 = None  # type: ignore

import fcntl
import os
import subprocess
import sys
import threading
import time

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "lib"))
import surface_cam as sc  # noqa: E402

DEV_OUT = "/dev/video61"
W, H, FPS = 1920, 1080, 30
CAP_W, CAP_H = 1632, 1224
SUITE = os.path.join(ROOT, "captures/test/suite")
# v4l2-ctl trims 4 B at END of frame (bytesused-data_offset from buffer start).
# Line still has 4 B CSI-2 header — LINE_OFF=4. LINE_OFF=0
# pulls headers into MIPI10 groups → grid every 4 px + false R/B.
STREAM_LINE_OFF = 4


class LockedTone:
    def __init__(self) -> None:
        self.lo: float | None = None
        self.hi: float | None = None
        self.scales: np.ndarray | None = None
        self._acc: list[tuple[np.ndarray, np.ndarray]] = []

    def learn(self, img16: np.ndarray, bgr: np.ndarray) -> None:
        if self.scales is not None:
            return
        self._acc.append((img16.astype(np.float32), bgr.astype(np.float32)))
        if len(self._acc) < 8:
            return
        raws = np.mean(np.stack([a[0] for a in self._acc], axis=0), axis=0)
        p1, p99 = np.percentile(raws, (1.0, 99.3))
        self.lo, self.hi = float(p1), float(max(p99, p1 + 16))
        stack = np.mean(np.stack([a[1] for a in self._acc], axis=0), axis=0)
        flat = stack.reshape(-1, 3)
        lum = flat.mean(axis=1)
        mid = flat[(lum > 25) & (lum < 220)]
        if len(mid) < 100:
            mid = flat
        means = np.maximum(mid.mean(axis=0), 1.0)
        # neutral grey-world — warm bias turned grey into yellow
        self.scales = np.clip(float(means.mean()) / means, 0.85, 1.22)
        self._acc.clear()
        print(
            f"WB locked raw[{self.lo:.0f}..{self.hi:.0f}] scales={self.scales}",
            flush=True,
        )

    def stretch16(self, img16: np.ndarray) -> np.ndarray:
        if self.lo is None or self.hi is None:
            return (img16 >> 2).astype(np.uint8)
        scale = 255.0 / max(self.hi - self.lo, 16.0)
        return np.clip((img16.astype(np.float32) - self.lo) * scale, 0, 255).astype(
            np.uint8
        )

    def apply_wb(self, bgr: np.ndarray) -> np.ndarray:
        if self.scales is None:
            return bgr
        return np.clip(bgr.astype(np.float32) * self.scales, 0, 255).astype(np.uint8)


def decode(cam: sc.Cam, raw: bytes, w: int, h: int, stride: int, tone: LockedTone) -> np.ndarray:
    need = stride * h
    if len(raw) < need:
        raw = raw + bytes(need - len(raw))
    img16 = sc.unpack_mipi10_u16(raw[:need], w, h, stride, line_off=STREAM_LINE_OFF)
    grey = tone.stretch16(img16)
    try:
        bgr = cv2.cvtColor(grey, cam.bayer or cv2.COLOR_BayerRG2BGR_EA)
    except cv2.error:
        bgr = cv2.cvtColor(grey, cv2.COLOR_BayerRG2BGR)
    bgr = sc._orient(bgr, cam)
    if tone.scales is None:
        tone.learn(img16, bgr)
        grey = tone.stretch16(img16)
        try:
            bgr = cv2.cvtColor(grey, cam.bayer or cv2.COLOR_BayerRG2BGR_EA)
        except cv2.error:
            bgr = cv2.cvtColor(grey, cv2.COLOR_BayerRG2BGR)
        bgr = sc._orient(bgr, cam)
    bgr = tone.apply_wb(bgr)
    return cv2.resize(bgr, (W, H), interpolation=cv2.INTER_LINEAR)


def _open_loopback() -> int:
    subprocess.run(
        ["v4l2-ctl", "-d", DEV_OUT, "-c", "keep_format=0"],
        capture_output=True,
    )
    subprocess.run(
        [
            "v4l2-ctl",
            "-d",
            DEV_OUT,
            "--set-fmt-video",
            f"width={W},height={H},pixelformat=YUYV",
            "--set-parm",
            str(FPS),
        ],
        capture_output=True,
    )
    try:
        subprocess.run(
            ["v4l2-ctl", "-d", DEV_OUT, "-c", "keep_format=1,sustain_framerate=1"],
            capture_output=True,
        )
    except OSError:
        pass
    fd = os.open(DEV_OUT, os.O_WRONLY | os.O_NONBLOCK)
    try:
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
    except OSError:
        pass
    return fd


def _write_yuyv(fd: int, yuyv: bytes) -> int:
    """Non-blocking write; full frame or drop (no queue)."""
    try:
        n = os.write(fd, yuyv)
        return n
    except BlockingIOError:
        return 0
    except OSError:
        return -1


def main() -> None:
    cam = sc.CAMS["back"]
    sc.sensor_runtime_cycle("ov8865", timeout=4.0)
    sc.run(["media-ctl", "-d", sc.MD, "-r"], check=False)
    time.sleep(0.2)
    dev = sc.media_setup(cam, CAP_W, CAP_H)
    sc.set_sensor_exposure("ov8865", cam.exposure, cam.gain)
    sub = sc.find_subdev("ov8865")
    if sub:
        subprocess.run(
            [
                "v4l2-ctl",
                "-d",
                sub,
                "--set-ctrl",
                "vertical_blanking=24,red_balance=1000,blue_balance=1300",
            ],
            capture_output=True,
        )
    stride = sc.get_stride(dev, CAP_W)
    si = sc.get_size_image(dev)
    fl = sc.v4l_stream_bytes(stride, CAP_H)
    print(
        f"capture {dev} {CAP_W}x{CAP_H} stride={stride} size_image={si} fl={fl}",
        flush=True,
    )

    out_fd = _open_loopback()
    cap = subprocess.Popen(
        [
            "v4l2-ctl",
            "-d",
            dev,
            "--stream-mmap=3",
            "--stream-count=0",
            "--stream-to=-",
            "--stream-poll",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    assert cap.stdout
    # larger pipe so v4l2-ctl does not split writes at 64 KiB mid-frame
    try:
        fcntl.fcntl(cap.stdout.fileno(), fcntl.F_SETPIPE_SZ, 1 << 21)
    except OSError:
        pass
    os.set_blocking(cap.stdout.fileno(), False)

    latest_raw: list[bytes | None] = [None]
    latest_yuyv: list[bytes | None] = [None]
    latest_bgr: list[np.ndarray | None] = [None]
    stop = threading.Event()
    stats = {"got": 0, "drop": 0, "dec": 0, "out": 0, "outfail": 0}

    def _reader() -> None:
        buf = bytearray()
        while not stop.is_set():
            try:
                chunk = cap.stdout.read(1 << 20)
            except BlockingIOError:
                chunk = b""
            if not chunk:
                if cap.poll() is not None:
                    print("v4l2-ctl died", cap.returncode, flush=True)
                    stop.set()
                    return
                time.sleep(0.001)
                continue
            buf.extend(chunk)
            extra = len(buf) // fl
            if extra == 0:
                continue
            if extra > 1:
                stats["drop"] += extra - 1
            raw = bytes(buf[(extra - 1) * fl : extra * fl])
            del buf[: extra * fl]
            latest_raw[0] = raw
            stats["got"] += extra

    def _cfr() -> None:
        period = 1.0 / FPS
        fd = out_fd
        while not stop.is_set():
            t0 = time.time()
            yuyv = latest_yuyv[0]
            if yuyv is not None:
                n = _write_yuyv(fd, yuyv)
                if n < 0:
                    try:
                        os.close(fd)
                    except OSError:
                        pass
                    try:
                        fd = _open_loopback()
                    except OSError as e:
                        stats["outfail"] += 1
                        print("loopback reopen", e, flush=True)
                        time.sleep(0.2)
                        continue
                elif n == 0:
                    stats["outfail"] += 1
                elif n == len(yuyv):
                    stats["out"] += 1
            time.sleep(max(0.0, period - (time.time() - t0)))

    threading.Thread(target=_reader, daemon=True).start()
    threading.Thread(target=_cfr, daemon=True).start()

    tone = LockedTone()
    n = 0
    t0 = time.time()
    t_log = t0
    last_seen = None
    while not stop.is_set():
        raw = latest_raw[0]
        if raw is None or raw is last_seen:
            time.sleep(0.002)
            continue
        last_seen = raw
        bgr = decode(cam, raw, CAP_W, CAP_H, stride, tone)
        latest_bgr[0] = bgr
        latest_yuyv[0] = sc.bgr_or_grey_to_yuyv(bgr)
        n += 1
        stats["dec"] += 1
        now = time.time()
        if now - t_log >= 1.5:
            fps = n / max(now - t0, 1e-6)
            print(
                f"LIVE_BACK n={n} unique_fps={fps:.1f} drop={stats['drop']} "
                f"out={stats['out']} mean={float(bgr.mean()):.1f} std={float(bgr.std()):.1f}",
                flush=True,
            )
            t_log = now
        if n in (12, 40):
            cv2.imwrite(
                os.path.join(SUITE, f"back-live-stable-{n}.jpg"),
                bgr,
                [int(cv2.IMWRITE_JPEG_QUALITY), 88],
            )
            if n == 12:
                cv2.imwrite(
                    os.path.join(SUITE, "back-live-now.jpg"),
                    bgr,
                    [int(cv2.IMWRITE_JPEG_QUALITY), 88],
                )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
