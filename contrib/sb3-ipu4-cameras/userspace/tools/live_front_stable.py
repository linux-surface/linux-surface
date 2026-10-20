#!/usr/bin/env python3
"""Stable LIVE Front (ov5693) → /dev/video62.

Same recipe as Back:
  • frame package = stride*h-4 (v4l2-ctl strips data_offset=4), LINE_OFF=4
  • newest frame only — no RAW/ffmpeg queue
  • YUYV non-block straight to loopback (ffmpeg clogged pipe → stale frames)
Capture 1296×972 (2592×1944 kills ISYS). IPU = one sensor.
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

DEV_OUT = "/dev/video62"
W, H, FPS = 1920, 1080, 30
CAP_W, CAP_H = 1296, 972
SUITE = os.path.join(ROOT, "captures/test/suite")
STREAM_LINE_OFF = 4
# CSI leftovers / truncated stride*h-4: red bar on the right, blue on the bottom.
CROP_RIGHT = 16
CROP_BOTTOM = 12


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
    hh, ww = bgr.shape[:2]
    r = min(CROP_RIGHT, max(ww - 8, 0))
    btm = min(CROP_BOTTOM, max(hh - 8, 0))
    if r or btm:
        bgr = bgr[0 : hh - btm, 0 : ww - r]
    return cv2.resize(bgr, (W, H), interpolation=cv2.INTER_LINEAR)


def _open_loopback() -> int:
    subprocess.run(["v4l2-ctl", "-d", DEV_OUT, "-c", "keep_format=0"], capture_output=True)
    # exclusive_caps=1: writer is VIDEO_OUTPUT. --set-fmt-video touches CAPTURE
    # and leaves 640×480 BGR4 — FHD YUYV writes then fail.
    subprocess.run(
        [
            "v4l2-ctl",
            "-d",
            DEV_OUT,
            "--set-fmt-video-out",
            f"width={W},height={H},pixelformat=YUYV",
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
    try:
        os.chmod(DEV_OUT, 0o666)
    except OSError:
        pass
    fd = os.open(DEV_OUT, os.O_WRONLY | os.O_NONBLOCK)
    try:
        flg = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flg | os.O_NONBLOCK)
    except OSError:
        pass
    return fd


def _write_yuyv(fd: int, yuyv: bytes) -> int:
    """Non-blocking write; full frame or drop (no queue)."""
    try:
        return os.write(fd, yuyv)
    except BlockingIOError:
        return 0
    except OSError:
        return -1


def main() -> None:
    import ipu_guard as ig

    ok, msg = ig.health_check()
    if not ok:
        print(f"ABORT IPU unhealthy: {msg}", flush=True)
        raise SystemExit(2)
    if sc.sensor_runtime_status("ov8865") == "active":
        sc._sensor_pm_write("ov8865", "auto")
        sc.sensor_runtime_cycle("ov8865", timeout=4.0)

    cam = sc.CAMS["front"]
    sc._sensor_pm_write("ov5693", "auto")
    if not sc.sensor_runtime_cycle("ov5693", timeout=8.0):
        print("ABORT ov5693 not suspended", sc.sensor_runtime_status("ov5693"), flush=True)
        raise SystemExit(3)
    sc.run(["media-ctl", "-d", sc.MD, "-r"], check=False)
    time.sleep(0.2)
    dev = sc.media_setup(cam, CAP_W, CAP_H)
    sc.set_sensor_exposure("ov5693", cam.exposure, cam.gain)
    stride = sc.get_stride(dev, CAP_W)
    si = sc.get_size_image(dev)
    fl = sc.v4l_stream_bytes(stride, CAP_H)
    print(
        f"capture {dev} {CAP_W}x{CAP_H} stride={stride} size_image={si} fl={fl}",
        flush=True,
    )

    # Single STREAMON. Separate warmup (STREAMON→STREAMOFF) with DPHY error
    # can cause "stream stop time out" and kill FW. SIGKILL (-k) still
    # overlaps the next STREAMON. First frames may arrive after >8s.
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
    n_log = 0
    last_seen = None
    t_watch = t0
    while not stop.is_set():
        raw = latest_raw[0]
        if raw is None or raw is last_seen:
            idle = time.time() - t_watch
            if stats["got"] == 0 and idle > 12:
                print(
                    f"WAIT 0 frames {idle:.0f}s — keeping STREAMON (no STREAMOFF)",
                    flush=True,
                )
                t_watch = time.time()
            time.sleep(0.002)
            continue
        last_seen = raw
        t_watch = time.time()
        bgr = decode(cam, raw, CAP_W, CAP_H, stride, tone)
        latest_bgr[0] = bgr
        latest_yuyv[0] = sc.bgr_or_grey_to_yuyv(bgr)
        n += 1
        stats["dec"] += 1
        now = time.time()
        if now - t_log >= 1.5:
            inst = (n - n_log) / max(now - t_log, 1e-6)
            print(
                f"LIVE_FRONT n={n} unique_fps={inst:.1f} drop={stats['drop']} "
                f"out={stats['out']} mean={float(bgr.mean()):.1f} std={float(bgr.std()):.1f}",
                flush=True,
            )
            t_log = now
            n_log = n
        if n in (12, 40):
            cv2.imwrite(
                os.path.join(SUITE, f"front-live-stable-{n}.jpg"),
                bgr,
                [int(cv2.IMWRITE_JPEG_QUALITY), 88],
            )
            if n == 12:
                cv2.imwrite(
                    os.path.join(SUITE, "front-live-now.jpg"),
                    bgr,
                    [int(cv2.IMWRITE_JPEG_QUALITY), 88],
                )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
