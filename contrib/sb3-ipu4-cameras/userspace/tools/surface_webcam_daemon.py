#!/usr/bin/env python3
"""Surface Book 3 — 3× v4l2loopback like normal webcams (Chrome / webcamtests.com).

IPU = one sensor at a time. Daemon:
  • keeps continuous YUYV output on /dev/video60/61/62 (exclusive_caps=1) → Chrome sees them
  • live stream on the camera opened by the browser (fuser); others = last frame
  • no v4l2-ctl restart every N frames (that caused black flicker)

  PYTHONPATH=lib python3 -u tools/surface_webcam_daemon.py
"""
from __future__ import annotations

try:
    from pycompat import MODERN_PY, CV2_V5, bgr_to_yuyv_u8  # noqa: F401
except ImportError:
    MODERN_PY = False
    CV2_V5 = False
    bgr_to_yuyv_u8 = None  # type: ignore

import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "lib"))
import ipu_guard as ig  # noqa: E402
import surface_cam as sc  # noqa: E402

# Front = video60 (Chrome default). Back = 61. IR = 62 (exclusive_caps=0 for Howdy).
# Full HD @ 30 fps front/back. IPU capture in safe mode, scale to 1080p.
CFG = {
    "front": {
        "dev": "/dev/video60",
        "w": 1920,
        "h": 1080,
        "fps": 30,
        "cap_w": 1296,
        "cap_h": 972,
    },
    "back": {
        "dev": "/dev/video61",
        "w": 1920,
        "h": 1080,
        "fps": 30,
        "cap_w": 1632,
        "cap_h": 1224,
    },
    "ir": {
        "dev": "/dev/video62",
        "w": 640,
        "h": 480,
        "fps": 15,
        "cap_w": 640,
        "cap_h": 480,
    },
}
ORDER = ("front", "back", "ir")
LABELS = "Surface-Front,Surface-Back,Surface-IR-Howdy"
VIDEO_NR = "60,61,62"
# Chrome: Front+Back exclusive=1; IR=0 → Howdy reads while ffmpeg writes
EXCLUSIVE_CAPS = "1,1,0"


def _bgr_to_yuyv(bgr: np.ndarray) -> bytes:
    yuv = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV)
    y, u, v = cv2.split(yuv)
    h, w = y.shape
    out = np.empty((h, w * 2), dtype=np.uint8)
    out[:, 0::4] = y[:, 0::2]
    out[:, 1::4] = u[:, 0::2]
    out[:, 2::4] = y[:, 1::2]
    out[:, 3::4] = v[:, 0::2]
    return out.tobytes()


def _grey_to_yuyv(grey: np.ndarray) -> bytes:
    if grey.ndim == 3:
        grey = cv2.cvtColor(grey, cv2.COLOR_BGR2GRAY)
    return _bgr_to_yuyv(cv2.cvtColor(grey, cv2.COLOR_GRAY2BGR))


def _placeholder(name: str, w: int, h: int) -> np.ndarray:
    img = np.full((h, w, 3), 28, dtype=np.uint8)
    # light noise so there is no "static image" with a dead IPU
    noise = np.random.randint(0, 18, (h, w, 3), dtype=np.uint8)
    img = cv2.add(img, noise)
    cv2.putText(
        img,
        f"Surface-{name}",
        (24, h // 2 - 20),
        cv2.FONT_HERSHEY_SIMPLEX,
        1.0,
        (200, 200, 200),
        2,
        cv2.LINE_AA,
    )
    cv2.putText(
        img,
        "IPU recovering — live image soon",
        (24, h // 2 + 24),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (160, 160, 160),
        1,
        cv2.LINE_AA,
    )
    return img


class LoopWriter:
    """Continuous ffmpeg → v4l2loopback (YUYV — Chrome/V4L2 and PipeWire get frames)."""

    def __init__(self, name: str, dev: str, w: int, h: int, fps: float):
        self.name = name
        self.dev = dev
        self.w = w
        self.h = h
        self.fps = fps
        self._ff: subprocess.Popen | None = None
        self._lock = threading.Lock()
        self.last = _placeholder(name, w, h)
        self._stop = threading.Event()
        self.live = False  # True = capture_session pushes frames; hold_loop is quiet
        self._thr = threading.Thread(target=self._hold_loop, daemon=True)
        self._restarts = 0
        self._last_err = ""

    def start(self) -> None:
        for attempt in range(8):
            self._kill_ff()
            try:
                os.chmod(self.dev, 0o666)
            except OSError:
                pass
            self._start_ff()
            try:
                self.push(self.last)
                if self._ff and self._ff.stdin:
                    self._ff.stdin.flush()
            except Exception as e:
                self._last_err = str(e)
            time.sleep(0.3)
            if self._ff and self._ff.poll() is None:
                print(f"writer {self.name} → {self.dev} OK", flush=True)
                break
            print(f"writer {self.name} retry {attempt+1}: {self._last_err or 'dead'}", flush=True)
        else:
            print(f"writer {self.name} FAILED to stay up", flush=True)
        self._thr.start()

    def _kill_ff(self) -> None:
        if not self._ff:
            return
        try:
            if self._ff.stdin:
                self._ff.stdin.close()
        except Exception:
            pass
        try:
            self._ff.kill()
            self._ff.wait(timeout=1)
        except Exception:
            pass
        self._ff = None

    def _start_ff(self) -> None:
        # BGR→YUYV raw, fixed CFR. MJPEG gave track 1920x1080@30 with videoWidth=0
        # in Chrome (PipeWire/V4L2 did not decode loopback MJPEG frames).
        # keep_format=1 blocks MJPG→YUYV change — clear before starting writer.
        try:
            subprocess.run(
                ["v4l2-ctl", "-d", self.dev, "-c", "keep_format=0"],
                check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        except OSError:
            pass
        self._restarts += 1
        fps = max(int(self.fps), 1)
        self._ff = subprocess.Popen(
            [
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-f", "rawvideo", "-pix_fmt", "bgr24",
                "-s", f"{self.w}x{self.h}",
                "-framerate", str(fps),
                "-i", "pipe:0",
                "-pix_fmt", "yuyv422",
                "-r", str(fps),
                "-fps_mode", "cfr",
                "-f", "v4l2",
                self.dev,
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def push(self, bgr: np.ndarray) -> None:
        if bgr.shape[1] != self.w or bgr.shape[0] != self.h:
            bgr = cv2.resize(bgr, (self.w, self.h), interpolation=cv2.INTER_AREA)
        if bgr.ndim == 2:
            bgr = cv2.cvtColor(bgr, cv2.COLOR_GRAY2BGR)
        self.last = bgr
        payload = np.ascontiguousarray(bgr).tobytes()
        with self._lock:
            for attempt in range(3):
                if self._ff is None or self._ff.poll() is not None:
                    if self._restarts and self._restarts % 15 == 1:
                        print(f"writer {self.name} restart #{self._restarts}", flush=True)
                    self._kill_ff()
                    try:
                        os.chmod(self.dev, 0o666)
                    except OSError:
                        pass
                    self._start_ff()
                    time.sleep(0.05)
                try:
                    assert self._ff and self._ff.stdin
                    self._ff.stdin.write(payload)
                    # do not flush every frame — fewer BrokenPipe errors
                    if attempt == 0 and self._restarts <= 1:
                        self._ff.stdin.flush()
                    return
                except (BrokenPipeError, OSError, AssertionError) as e:
                    self._last_err = str(e)
                    self._kill_ff()
                    if attempt == 2:
                        return

    def _hold_loop(self) -> None:
        """Always repeat last at fixed FPS — even during live (live only updates last).
        Without live: refresh noisy placeholder ~2×/s so there is no "static image"."""
        period = 1.0 / max(self.fps, 1.0)
        n = 0
        while not self._stop.is_set():
            t0 = time.time()
            try:
                if not self.live and (n % max(int(self.fps) // 2, 1) == 0):
                    self.last = _placeholder(self.name, self.w, self.h)
                self.push(self.last)
            except Exception as e:
                print(f"hold {self.name}: {e}", flush=True)
            n += 1
            dt = time.time() - t0
            time.sleep(max(0.0, period - dt))

    def stop(self) -> None:
        self._stop.set()
        with self._lock:
            self._kill_ff()


def ensure_loopbacks() -> None:
    conf = Path("/etc/modprobe.d/00-surface-v4l2loopback.conf")
    want = (
        f"options v4l2loopback devices=3 video_nr={VIDEO_NR} "
        f"card_label={LABELS} "
        f"exclusive_caps={EXCLUSIVE_CAPS} max_buffers=8\n"
    )
    try:
        if not conf.exists() or "Surface-Front,Surface-Back,Surface-IR-Howdy" not in conf.read_text() \
           or f"exclusive_caps={EXCLUSIVE_CAPS}" not in conf.read_text():
            conf.write_text(want)
    except PermissionError:
        pass
    need = any(not Path(CFG[n]["dev"]).exists() for n in CFG)
    exc = Path("/sys/module/v4l2loopback/parameters/exclusive_caps")
    cur = exc.read_text().strip() if exc.exists() else ""
    name60 = ""
    try:
        name60 = Path("/sys/class/video4linux/video60/name").read_text().strip()
    except OSError:
        pass
    # Front must be video60; IR exclusive = N (last flag in Y,Y,N)
    want_reload = (
        need
        or name60 != "Surface-Front"
        or not cur.startswith("Y,Y,N")
    )
    if want_reload and os.geteuid() == 0:
        subprocess.run(["modprobe", "-r", "v4l2loopback"], check=False)
        time.sleep(0.3)
        subprocess.run(
            [
                "modprobe", "v4l2loopback",
                "devices=3", f"video_nr={VIDEO_NR}",
                f"card_label={LABELS}",
                f"exclusive_caps={EXCLUSIVE_CAPS}", "max_buffers=8",
            ],
            check=False,
        )
        time.sleep(0.4)
    elif want_reload:
        print(
            f"webcamd: loopback map warning (video60={name60!r}, caps={cur!r}) — needs root/remodeprobe",
            flush=True,
        )
    for n in CFG:
        p = Path(CFG[n]["dev"])
        if p.exists():
            try:
                os.chmod(p, 0o666)
            except OSError:
                pass


def readers_of(dev: str) -> set[int]:
    """PIDs of processes holding the device (excluding ffmpeg / our Python)."""
    out: set[int] = set()
    me = os.getpid()
    for pid in Path("/proc").iterdir():
        if not pid.name.isdigit():
            continue
        p = int(pid.name)
        if p == me:
            continue
        try:
            cmd = (pid / "cmdline").read_bytes().replace(b"\0", b" ").decode("utf-8", "ignore")
        except OSError:
            continue
        if not cmd or "ffmpeg" in cmd:
            continue
        if "surface_webcam_daemon" in cmd or "surface_v4l_bridge" in cmd:
            continue
        fd_dir = pid / "fd"
        try:
            for fd in fd_dir.iterdir():
                try:
                    target = os.readlink(fd)
                except OSError:
                    continue
                if target == dev:
                    out.add(p)
                    break
        except OSError:
            continue
    return out


def pick_active(writers: dict[str, LoopWriter], current: str) -> str:
    # Control ONLY via file — fuser/Chrome does not switch IPU (was hanging front).
    hint = Path("/run/surface-webcam/active")
    if hint.exists():
        t = hint.read_text().strip()
        if t in CFG:
            return t
    return current if current in CFG else "front"


def capture_session(name: str, writer: LoopWriter, stop: threading.Event) -> None:
    """Live RAW→loopback (pipe, no file rotation)."""
    cam = sc.CAMS[name]
    cfg = CFG[name]
    w, h, fps = cfg["w"], cfg["h"], cfg["fps"]
    cap_w, cap_h = cfg.get("cap_w", w), cfg.get("cap_h", h)
    try:
        ig.require_healthy()
        ig.assert_safe_resolution(name, cap_w, cap_h)
    except Exception as e:
        print(f"skip {name}: {e}", flush=True)
        return

    try:
        session = ig.ipu_session(f"webcam-{name}", timeout_sec=15)
        guard = session.__enter__()
    except Exception as e:
        print(f"skip {name}: IPU lock {e}", flush=True)
        return

    cap: subprocess.Popen | None = None
    try:
        # IR LED BEFORE media-ctl/stream — during stream i2c fails (Remote I/O)
        if cam.ir:
            for _ in range(5):
                sc.ir_led(True)
                time.sleep(0.1)

        guard.require_healthy()
        # WARNING: full-res DPHY kick (2592) can kill ISYS (EIO) — not used in daemon.
        # Do NOT enable sensor power before media_setup: mode is written on
        # resume from saved fmt; media_setup does cycle→fmt→on→ctrls in the right order.
        dev = sc.media_setup(cam, cap_w, cap_h)
        sc.set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
        # Ensure fmt = cap_w×cap_h (after DPHY kick Size Image can stay full-res)
        subprocess.run(
            [
                "v4l2-ctl", "-d", dev, "--set-fmt-video",
                f"width={cap_w},height={cap_h},pixelformat={cam.fourcc}",
            ],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        stride = sc.get_stride(dev, cap_w)
        fl = sc.frame_bytes(stride, cap_h, cam, cap_w, dev=dev)
        # sanity: Size Image must not match a different mode
        expect = stride * cap_h
        if fl > expect * 1.05 or fl < expect * 0.5:
            print(f"warn {name}: fl={fl} vs stride*h={expect} — using stride*h", flush=True)
            fl = expect

        # fast AE (fail-fast) — do not block session when STREAMON fails
        if not cam.ir:
            try:
                sc.auto_expose(cam, dev, cap_w, cap_h, stride, target=cam.target_mean, rounds=2)
                sc.set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
            except Exception as e:
                print(f"AE {name} skip: {e}", flush=True)

        writer.live = True
        cap = subprocess.Popen(
            [
                "v4l2-ctl", "-d", dev,
                "--stream-mmap=4", "--stream-count=0",
                "--stream-to=-", "--stream-poll",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        assert cap.stdout is not None

        def _kill_on_stop() -> None:
            stop.wait()
            try:
                if cap and cap.poll() is None:
                    cap.kill()
            except Exception:
                pass

        threading.Thread(target=_kill_on_stop, daemon=True).start()
        try:
            os.set_blocking(cap.stdout.fileno(), False)
        except Exception:
            pass

        print(
            f"LIVE {name} capture {cap_w}x{cap_h} → loopback {w}x{h}@{fps} ← {dev} → {cfg['dev']} fl={fl}",
            flush=True,
        )
        buf = b""
        n = 0
        period = 1.0 / max(fps, 1.0)
        t_start = time.time()
        t_last_frame = t_start
        while not stop.is_set() and cap.poll() is None:
            try:
                chunk = cap.stdout.read(min(max(fl - len(buf), 1), 65536))
            except BlockingIOError:
                chunk = b""
            if not chunk:
                # NEVER DPHY/full-res kick — that kills ISYS. Safe session restart only.
                if name == "front" and n == 0 and (time.time() - t_start) > 6.0:
                    print("front stall: 0 frames in 6s — safe restart (no DPHY)", flush=True)
                    break
                if n > 0 and (time.time() - t_last_frame) > 8.0:
                    print(f"{name} stall: no frames for 8s — safe restart", flush=True)
                    break
                time.sleep(0.005)
                continue
            buf += chunk
            while len(buf) >= fl and not stop.is_set():
                raw = buf[:fl]
                buf = buf[fl:]
                if len(buf) >= fl:
                    frames = len(buf) // fl
                    buf = buf[frames * fl :]
                t_frame = time.time()
                img = sc.frame_from_raw(cam, raw, cap_w, cap_h, stride)
                if cam.ir:
                    bgr = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR) if img.ndim == 2 else img
                    if float(bgr.mean()) < 40:
                        g = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
                        g = cv2.normalize(g, None, 0, 255, cv2.NORM_MINMAX)
                        clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
                        g = clahe.apply(g)
                        bgr = cv2.cvtColor(g, cv2.COLOR_GRAY2BGR)
                else:
                    bgr = sc.brighten_bgr(img, target=float(cam.target_mean))
                if bgr.shape[1] != w or bgr.shape[0] != h:
                    bgr = cv2.resize(bgr, (w, h), interpolation=cv2.INTER_AREA)
                # only update frame — hold_loop streams CFR to loopback
                writer.last = bgr
                n += 1
                t_last_frame = time.time()
                if n % 40 == 0:
                    print(f"  {name} frames={n} mean={float(bgr.mean()):.1f}", flush=True)
                dt = time.time() - t_frame
                time.sleep(max(0.0, period - dt))
        if name == "front" and n == 0:
            print("front produced 0 frames this session", flush=True)
    except Exception as e:
        print(f"capture {name} err: {e}", flush=True)
    finally:
        writer.live = False
        if cap is not None:
            try:
                cap.kill()
                cap.wait(timeout=2)
            except Exception:
                pass
        if cam.ir:
            sc.ir_led(False)
        try:
            session.__exit__(None, None, None)
        except Exception:
            pass
        print(f"stop live {name}", flush=True)


def main() -> None:
    Path("/run/surface-webcam").mkdir(parents=True, exist_ok=True)
    try:
        os.chmod("/run/surface-webcam", 0o755)
    except OSError:
        pass
    ensure_loopbacks()
    for p in ("/run/surface-ipu",):
        Path(p).mkdir(parents=True, exist_ok=True)
        try:
            os.chmod(p, 0o1777)
            for f in Path(p).iterdir():
                os.chmod(f, 0o666)
        except OSError:
            pass

    # Front first (video60 = browser default)
    writers: dict[str, LoopWriter] = {}
    for n in ("front", "back", "ir"):
        writers[n] = LoopWriter(n, CFG[n]["dev"], CFG[n]["w"], CFG[n]["h"], CFG[n]["fps"])
        writers[n].start()
        time.sleep(0.2)
    print("webcamd: loopbacks ready (Surface-Front / Surface-Back / Surface-IR-Howdy)", flush=True)

    stop_cap = threading.Event()
    active = "front"
    cap_thr: threading.Thread | None = None
    dying = threading.Event()

    def _shutdown(*_):
        dying.set()
        stop_cap.set()
        for w in writers.values():
            w.stop()
        sc.ir_led(False)
        sys.exit(0)

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    while not dying.is_set():
        try:
            ig.require_healthy()
        except ig.IpuUnhealthy as e:
            # ISYS dead — do not spam every 0.5s; wait for reboot/remodeprobe
            print(f"IPU unhealthy — idle 30s ({e})", flush=True)
            time.sleep(30)
            continue
        want = pick_active(writers, active)
        if want != active or cap_thr is None or not cap_thr.is_alive():
            if cap_thr and cap_thr.is_alive():
                stop_cap.set()
                cap_thr.join(timeout=3)
                if cap_thr.is_alive():
                    print(f"warn: capture {active} still stopping", flush=True)
                    # hard reset IPU capture
                    subprocess.run(
                        ["bash", "-c", "pkill -9 -x v4l2-ctl 2>/dev/null || true"],
                        check=False,
                    )
                    cap_thr.join(timeout=2)
            stop_cap = threading.Event()
            active = want
            Path("/run/surface-webcam/active").write_text(active + "\n")
            print(f"active → {active}", flush=True)
            cap_thr = threading.Thread(
                target=capture_session,
                args=(active, writers[active], stop_cap),
                daemon=True,
            )
            cap_thr.start()
        time.sleep(0.5)


if __name__ == "__main__":
    main()
