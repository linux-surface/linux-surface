#!/usr/bin/env python3
"""Bridge RAW IPU4 → v4l2loopback (YUYV) for guvcview / browser / cameractrls.

Usage:
  PYTHONPATH=lib python3 tools/surface_v4l_bridge.py back --device /dev/video61
  PYTHONPATH=lib python3 tools/surface_v4l_bridge.py front --device /dev/video62
  PYTHONPATH=lib python3 tools/surface_v4l_bridge.py ir --device /dev/video60

Note: IPU4 ISYS — usually one camera at a time. SoftISP intentionally skipped (hang).
"""
from __future__ import annotations

try:
    from pycompat import MODERN_PY, CV2_V5, bgr_to_yuyv_u8  # noqa: F401
except ImportError:
    MODERN_PY = False
    CV2_V5 = False
    bgr_to_yuyv_u8 = None  # type: ignore

import argparse
import os
import signal
import subprocess
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "lib"))
import surface_cam as sc  # noqa: E402


def ensure_loop(dev: str, label: str) -> None:
    if os.path.exists(dev):
        return
    nr = int(dev.replace("/dev/video", ""))
    subprocess.run(["modprobe", "-r", "v4l2loopback"], check=False)
    time.sleep(0.3)
    # keep video60 for Howdy if possible — load 3 devices
    subprocess.run(
        [
            "modprobe",
            "v4l2loopback",
            "devices=3",
            "video_nr=60,61,62",
            "card_label=Surface-IR-Howdy,Surface-Back,Surface-Front",
            "exclusive_caps=1,1,1",
            "max_buffers=4",
        ],
        check=False,
    )
    time.sleep(0.4)
    if not os.path.exists(dev):
        raise SystemExit(f"missing {dev} after modprobe")
    os.chmod(dev, 0o666)


def bgr_to_yuyv(bgr: np.ndarray) -> bytes:
    yuv = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV)
    y, u, v = cv2.split(yuv)
    h, w = y.shape
    # YUYV packed
    out = np.empty((h, w * 2), dtype=np.uint8)
    out[:, 0::4] = y[:, 0::2]
    out[:, 1::4] = u[:, 0::2]
    out[:, 2::4] = y[:, 1::2]
    out[:, 3::4] = v[:, 0::2]
    return out.tobytes()


def grey_to_yuyv(grey: np.ndarray) -> bytes:
    if grey.ndim == 3:
        grey = cv2.cvtColor(grey, cv2.COLOR_BGR2GRAY)
    bgr = cv2.cvtColor(grey, cv2.COLOR_GRAY2BGR)
    return bgr_to_yuyv(bgr)


def stream_cam(cam_name: str, loop_dev: str, width: int | None, height: int | None, fps: float) -> None:
    import ipu_guard as ig

    cam = sc.CAMS[cam_name]
    if width is None or height is None:
        width, height = cam.modes[0]
    ig.assert_safe_resolution(cam_name, width, height)
    ensure_loop(loop_dev, cam_name)

    with ig.ipu_session(f"bridge-{cam_name}", timeout_sec=45) as guard:
        guard.require_healthy()
        sc.set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
        dev = sc.media_setup(cam, width, height)
        stride = sc.get_stride(dev, width)
        fl = sc.frame_bytes(stride, height, cam, width)

        ff = subprocess.Popen(
            [
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-fflags", "nobuffer", "-flags", "low_delay",
                "-f", "rawvideo", "-pix_fmt", "yuyv422",
                "-s", f"{width}x{height}", "-framerate", str(fps),
                "-i", "pipe:0",
                "-f", "v4l2", "-pix_fmt", "yuyv422",
                "-s", f"{width}x{height}", loop_dev,
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        assert ff.stdin is not None

        stop = False

        def _sig(*_):
            nonlocal stop
            stop = True

        signal.signal(signal.SIGINT, _sig)
        signal.signal(signal.SIGTERM, _sig)

        # /dev/shm — not /tmp (quota + less I/O)
        dest = f"/dev/shm/surface-bridge-{cam_name}.raw"
        if os.path.exists(dest):
            try:
                os.remove(dest)
            except OSError:
                # root-owned leftover (SELinux/sticky) — use user-specific name
                dest = f"/dev/shm/surface-bridge-{cam_name}-{os.getuid()}.raw"
                try:
                    os.remove(dest)
                except OSError:
                    pass
        open(dest, "wb").close()
        try:
            os.chmod(dest, 0o666)
        except OSError:
            pass
        cap = subprocess.Popen(
            [
                "v4l2-ctl", "-d", dev,
                "--stream-mmap=4", "--stream-count=0",
                f"--stream-to={dest}", "--stream-poll",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.35)
        if cam.ir:
            for _ in range(4):
                sc.ir_led(True)
                time.sleep(0.1)

        print(f"BRIDGE {cam_name} {width}x{height} → {loop_dev} (ctrl-C stop)", flush=True)
        offset = 0
        n = 0
        last_health = time.time()
        try:
            while not stop and ff.poll() is None and cap.poll() is None:
                if time.time() - last_health > 2.0:
                    last_health = time.time()
                    ok, msg = ig.health_check()
                    if not ok:
                        print(f"IPU unhealthy: {msg} — stopping bridge", flush=True)
                        break
                try:
                    size = os.path.getsize(dest)
                except OSError:
                    time.sleep(0.02)
                    continue
                # CRITICAL: process ONLY the newest frame — otherwise minute-long lag + 1 FPS
                if size - offset >= fl:
                    frames_avail = (size - offset) // fl
                    if frames_avail > 1:
                        offset += (frames_avail - 1) * fl
                    with open(dest, "rb") as f:
                        f.seek(offset)
                        raw = f.read(fl)
                    offset += fl
                    if len(raw) < fl:
                        time.sleep(0.005)
                        continue
                    img = sc.frame_from_raw(cam, raw, width, height, stride)
                    if cam.ir:
                        if img.ndim == 2:
                            h, w = img.shape
                        else:
                            h, w = img.shape[:2]
                        if (w, h) != (width, height) and (h, w) == (width, height):
                            img = cv2.resize(img, (width, height))
                        payload = grey_to_yuyv(img)
                    else:
                        if img.shape[1] != width or img.shape[0] != height:
                            img = cv2.resize(img, (width, height))
                        payload = bgr_to_yuyv(img)
                    try:
                        ff.stdin.write(payload)
                        ff.stdin.flush()
                        n += 1
                    except BrokenPipeError:
                        print("ffmpeg broken", flush=True)
                        stop = True
                        break
                    if cam.ir and n % 8 == 0:
                        sc.ir_led(True)
                    if n % 30 == 0:
                        print(f"  frames={n} (dropped_backlog ok)", flush=True)
                # aggressive file rotation — do not let /dev/shm grow
                if offset > fl * 30:
                    try:
                        cap.kill()
                        cap.wait(timeout=1)
                    except Exception:
                        pass
                    open(dest, "wb").close()
                    offset = 0
                    cap = subprocess.Popen(
                        [
                            "v4l2-ctl", "-d", dev,
                            "--stream-mmap=4", "--stream-count=0",
                            f"--stream-to={dest}", "--stream-poll",
                        ],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    time.sleep(0.15)
                time.sleep(0.001)
        finally:
            if cam.ir:
                sc.ir_led(False)
            try:
                cap.kill()
            except Exception:
                pass
            try:
                ff.stdin.close()
            except Exception:
                pass
            try:
                ff.kill()
            except Exception:
                pass
            print("bridge stop", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("cam", choices=["back", "front", "ir"])
    ap.add_argument("--device", default=None)
    ap.add_argument("--width", type=int, default=None)
    ap.add_argument("--height", type=int, default=None)
    ap.add_argument("--fps", type=float, default=15.0)
    args = ap.parse_args()
    defaults = {"back": "/dev/video61", "front": "/dev/video62", "ir": "/dev/video60"}
    stream_cam(args.cam, args.device or defaults[args.cam], args.width, args.height, args.fps)


if __name__ == "__main__":
    main()
