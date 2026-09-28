#!/usr/bin/env python3
"""Always-on Surface Book 3 webcams — live frames, no placeholders, no terminal.

IPU4 = one sensor at a time. Front (ov5693) is the default camera.
Resolve loopbacks by card name: Surface-Front, Surface-Back, Surface-IR-Howdy
(/dev/video* numbers may be 60/61/62 or swapped).
Back STREAMON only when something actually opens Surface-Back.
IR STREAMON only when Surface-IR-Howdy has a reader (Howdy preempts Front/Back).
IR LED: continuous ON while IR streams (no visible blink). Status tracks howdy vs preview.

SURFACE_WEBCAM_IR=1 enables IR (required for Howdy). Set 0 to keep IR off.
"""
from __future__ import annotations

import fcntl
import json
import os
import signal
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "lib"))
import ipu_guard as ig  # noqa: E402
import surface_cam as sc  # noqa: E402

# Howdy commit: default ON when env unset; explicit 0/false disables.
_ir_env = os.environ.get("SURFACE_WEBCAM_IR", "1")
IR_ENABLED = _ir_env not in ("0", "false", "False", "")

def resolve_loopbacks() -> dict[str, str]:
    """Resolve by card name — Front should be the lowest number (V4L2 default)."""
    found: dict[str, str] = {}
    try:
        ents = Path("/sys/class/video4linux").iterdir()
    except OSError:
        ents = []
    for ent in ents:
        if not ent.name.startswith("video"):
            continue
        try:
            name = (ent / "name").read_text().strip()
        except OSError:
            continue
        if name == "Surface-Front":
            found["front"] = f"/dev/{ent.name}"
        elif name == "Surface-Back":
            found["back"] = f"/dev/{ent.name}"
        elif name == "Surface-IR-Howdy":
            found["ir"] = f"/dev/{ent.name}"
    found.setdefault("front", "/dev/video60")
    found.setdefault("back", "/dev/video61")
    found.setdefault("ir", "/dev/video62")
    return found


DEV = resolve_loopbacks()
CAP = {
    "front": {"w": 1296, "h": 972, "sensor": "ov5693", "crop_r": 16, "crop_b": 12, "crop_l": 8},
    "back": {"w": 1632, "h": 1224, "sensor": "ov8865", "crop_r": 16, "crop_b": 8, "crop_l": 8},
    "ir": {"w": 640, "h": 480, "sensor": "ov7251", "crop_r": 0, "crop_b": 0, "crop_l": 0},
}
# Front/back = FHD. IR = native after rot90 (640×480 → 480×640) — no FHD upscale.
OUT = {
    "front": (1920, 1080),
    "back": (1920, 1080),
    "ir": (480, 640),
}
OUT_W, OUT_H = OUT["front"]  # legacy alias for front/back paths
FPS = 30
STREAM_LINE_OFF = 4
HINT = Path("/run/surface-webcam/active")
STATUS = Path("/run/surface-webcam/status")
IR_OWNER = Path("/run/surface-webcam/ir_owner")  # preview | howdy | idle
IR_LED_MODE = Path("/run/surface-webcam/ir_led_mode")  # steady | blink | off
# Legacy marker — no longer blocks Front. Unlinked at start.
BACK_USED = Path("/run/surface-ipu/back-used")
# Windows powers ISYS down between cameras (~2–3 s) and rearms PHY from scratch.
SWITCH_HOLD = 0.3
SWITCH_DWELL = 1.0
SWITCH_WAIT = 6.0
IDLE_SWITCH = 0.3
FAIL_BACKOFF = 8.0
DEAD_BACKOFF = 120.0
FRONT_LOCK_HOLD = 2.0
LINGER_S = 0.5
ISYS_RUNTIME = Path(
    "/sys/devices/pci0000:00/0000:00:05.0/intel-ipu4-mmu0/intel-ipu60/power/runtime_status"
)
CACHE_DIR = Path("/var/cache/surface-webcam")
# Only our writer / v4l2-ctl. PipeWire holding the fd = a real reader
# (WP monitor does not hold the loopback while Video/Source is suspended).
# Do NOT match "pipewire" in cmdline — Chrome has --disable-webrtc-pipewire-camera
# and then the whole process dropped out of detection → black hold without STREAMON.
SKIP_COMMS = ("v4l2-ctl",)
LED = {
    "front": Path("/sys/class/leds/INT33BE_00::privacy_led/brightness"),
    "back": Path("/sys/class/leds/INT347A_00::privacy_led/brightness"),
}


def set_privacy_led(name: str, on: bool) -> None:
    p = LED.get(name)
    if p is None:
        return
    try:
        p.write_text("1\n" if on else "0\n")
    except OSError:
        pass
_PW_CACHE = {"t": 0.0, "running": set()}


def isys_runtime() -> str:
    try:
        return ISYS_RUNTIME.read_text().strip()
    except OSError:
        return "unknown"


def wait_isys_idle(timeout: float = 5.0, min_dwell: float = 2.0) -> bool:
    """Wait until ISYS firmware suspends — that resets AFE on every port.

    Windows does exactly this between Front/Back (~2–3 s). Do not rmmod.
    """
    t0 = time.time()
    saw: float | None = None
    while time.time() - t0 < timeout:
        st = isys_runtime()
        if st == "suspended":
            if saw is None:
                saw = time.time()
            if time.time() - saw >= min_dwell:
                print(
                    f"ISYS idle {time.time() - t0:.1f}s (dwell {time.time() - saw:.1f}s)",
                    flush=True,
                )
                return True
        else:
            saw = None
        time.sleep(0.1)
    print(f"ISYS still {isys_runtime()} after {timeout:.0f}s", flush=True)
    return False


def wait_switch_cycle() -> bool:
    """Po STREAMOFF zawsze dwell — skip gdy already-suspended = Front2 na brudnym PHY."""
    print(f"switch-settle {SWITCH_DWELL:.1f}s (isys={isys_runtime()})", flush=True)
    time.sleep(SWITCH_DWELL)
    return wait_isys_idle(timeout=max(SWITCH_WAIT, 3.0), min_dwell=2.0)


class LockedTone:
    def __init__(self) -> None:
        self.lo: float | None = None
        self.hi: float | None = None
        self.scales: np.ndarray | None = None
        self._acc: list[tuple[np.ndarray, np.ndarray]] = []

    def learn(self, img16: np.ndarray, bgr: np.ndarray) -> None:
        if self.scales is not None:
            p50 = float(np.percentile(img16, 50.0))
            mid = (self.lo + self.hi) * 0.5
            if mid > 1.0 and (p50 > mid * 2.4 or p50 < mid * 0.4):
                self.lo = self.hi = None
                self.scales = None
                self._acc.clear()
                print(f"WB relock p50={p50:.0f} mid={mid:.0f}", flush=True)
            else:
                return
        self._acc.append((img16.astype(np.float32), bgr.astype(np.float32)))
        if len(self._acc) < 8:
            return
        raws = np.mean(np.stack([a[0] for a in self._acc], axis=0), axis=0)
        p1, p95, p99, p995 = np.percentile(raws, (1.0, 95.0, 99.0, 99.5))
        if p99 >= 980 and p1 >= 200:
            self._acc.clear()
            print(f"WB skip clip raw p1={p1:.0f} p99={p99:.0f}", flush=True)
            return
        # a small phone screen must not pin hi at 1023
        if p995 > p95 * 2.0:
            hi = float(max(p95, p1 + 16.0))
        else:
            hi = float(max(min(p99, 900.0), p1 + 16.0))
        self.lo, self.hi = float(p1), hi
        stack = np.mean(np.stack([a[1] for a in self._acc], axis=0), axis=0)
        flat = stack.reshape(-1, 3)
        lum = flat.mean(axis=1)
        mid = flat[(lum > 25) & (lum < 220)]
        if len(mid) < 100:
            mid = flat
        means = np.maximum(mid.mean(axis=0), 1.0)
        # wide grey-world: teal/cyan walls → white (0.92–1.12 used to leave a cast)
        self.scales = np.clip(float(means.mean()) / means, 0.55, 1.85)
        self._acc.clear()
        print(
            f"WB locked raw[{self.lo:.0f}..{self.hi:.0f}] scales={self.scales}",
            flush=True,
        )

    def stretch16(self, img16: np.ndarray) -> np.ndarray:
        return self.stretch_any(img16)

    def stretch_any(self, img: np.ndarray) -> np.ndarray:
        x = img.astype(np.float32)
        if self.lo is None or self.hi is None:
            p1, p99 = np.percentile(x, (1.0, 99.0))
            if p99 >= 980 and p1 >= 200:
                return np.clip(x * (200.0 / 1023.0), 0, 255).astype(np.uint8)
            lo, hi = float(p1), float(max(p99, p1 + 16.0))
        else:
            lo, hi = self.lo, self.hi
        z = np.maximum(x - lo, 0.0)
        mid = max(hi - lo, 16.0)
        # Reinhard: hi → ~165, 1023 → ~240 — face kept, OLED highlights not crushed to 255
        b = mid * 0.55
        a = 165.0 * (mid + b) / mid
        y = a * z / (z + b)
        return np.clip(y, 0, 255).astype(np.uint8)

    def apply_wb_linear(self, bgr16: np.ndarray) -> np.ndarray:
        if self.scales is None:
            return bgr16
        return np.clip(bgr16.astype(np.float32) * self.scales, 0, 1023)


def detect_line_off(raw: bytes) -> int:
    """IPU: first frames have a 4 B CSI-2 header per line; later ones often do not."""
    if len(raw) < 4:
        return STREAM_LINE_OFF
    di, wcl, wch, ecc = raw[0], raw[1], raw[2], raw[3]
    # 0x2B/0x30/0x50 classic; 0x40 = VC1|DT0 seen on SB3 ov7251 face frames
    if wcl == 0 and wch == 0 and di in (0x2B, 0x30, 0x40, 0x50) and ecc in (0x00, 0x80):
        return 4
    return 0


def raw_good_line_frac(raw: bytes, h: int, stride: int, line_off: int) -> float:
    """Fraction of lines with a real payload (not 0xFF)."""
    if stride <= 8 or h <= 0 or len(raw) < stride:
        return 0.0
    good = 0
    rows = min(h, len(raw) // stride)
    off = line_off if line_off + 1 < stride else 0
    for y in range(rows):
        b = raw[y * stride + off]
        if b != 0xFF:
            good += 1
    return good / float(h)


def decode(
    cam: sc.Cam,
    raw: bytes,
    w: int,
    h: int,
    stride: int,
    tone: LockedTone,
    crop_r: int,
    crop_b: int,
    crop_l: int = 0,
) -> np.ndarray | None:
    need = stride * h
    if len(raw) < need:
        raw = raw + bytes(need - len(raw))
    line_off = detect_line_off(raw)
    # Proven face decode always uses line_off=4 (ir-face-clean.jpg).
    if cam.ir:
        line_off = 4
    if raw_good_line_frac(raw, h, stride, line_off) < 0.40:
        return None
    if cam.ir:
        grey = sc.finish_ir(
            sc.unpack_mipi10_ir(raw[:need], w, h, stride, line_off=line_off), cam
        )
        # Native 480×640 after rot90 — no letterbox / FHD upscale.
        return grey
    img16 = sc.unpack_mipi10_u16(raw[:need], w, h, stride, line_off=line_off)
    try:
        bgr16 = cv2.cvtColor(img16, cam.bayer or cv2.COLOR_BayerRG2BGR_EA)
    except cv2.error:
        bgr16 = cv2.cvtColor(img16, cv2.COLOR_BayerRG2BGR)
    bgr16 = sc._orient(bgr16, cam)
    if tone.scales is None:
        tone.learn(img16, tone.stretch_any(bgr16))
    bgr16 = tone.apply_wb_linear(bgr16)
    bgr = tone.stretch_any(bgr16)
    if tone.scales is None:
        flat = bgr.reshape(-1, 3).astype(np.float32)
        lum = flat.mean(axis=1)
        mid = flat[(lum > 25) & (lum < 220)]
        if len(mid) < 100:
            mid = flat
        means = np.maximum(mid.mean(axis=0), 1.0)
        preview = np.clip(float(means.mean()) / means, 0.55, 1.85)
        bgr = np.clip(bgr.astype(np.float32) * preview, 0, 255).astype(np.uint8)
    hh, ww = bgr.shape[:2]
    r = min(crop_r, max(ww - 8, 0))
    btm = min(crop_b, max(hh - 8, 0))
    left = min(crop_l, max(ww - 8, 0))
    if r or btm or left:
        bgr = bgr[0 : hh - btm, left : ww - r]
    if cam.tag == "front":
        yuv = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV)
        y = yuv[:, :, 0]
        yuv[:, :, 0] = cv2.addWeighted(y, 0.72, cv2.GaussianBlur(y, (0, 0), 0.85), 0.28, 0)
        yuv[:, :, 1] = cv2.GaussianBlur(yuv[:, :, 1], (0, 0), 2.1)
        yuv[:, :, 2] = cv2.GaussianBlur(yuv[:, :, 2], (0, 0), 2.1)
        bgr = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR)
    return cv2.resize(bgr, (OUT_W, OUT_H), interpolation=cv2.INTER_LINEAR)


VIDIOC_QUERYCAP = 0x80685600
V4L2_CAP_VIDEO_CAPTURE = 0x00000001


def _fd_has_capture(fd: int) -> bool:
    """exclusive_caps: writer must advertise CAPTURE or guvcview hides the camera."""
    buf = bytearray(104)
    try:
        fcntl.ioctl(fd, VIDIOC_QUERYCAP, buf)
    except OSError:
        return False
    dcaps = struct.unpack_from("I", buf, 88)[0]
    return bool(dcaps & V4L2_CAP_VIDEO_CAPTURE)


def _open_loopback(dev: str, w: int | None = None, h: int | None = None) -> int:
    ow = OUT_W if w is None else w
    oh = OUT_H if h is None else h
    last_err: OSError | None = None
    for attempt in range(4):
        subprocess.run(["v4l2-ctl", "-d", dev, "-c", "keep_format=0"], capture_output=True)
        subprocess.run(
            [
                "v4l2-ctl",
                "-d",
                dev,
                "--set-fmt-video-out",
                f"width={ow},height={oh},pixelformat=YUYV",
                "--set-fmt-video",
                f"width={ow},height={oh},pixelformat=YUYV",
                "--set-parm",
                str(FPS),
            ],
            capture_output=True,
        )
        try:
            subprocess.run(
                ["v4l2-ctl", "-d", dev, "-c", "keep_format=1,sustain_framerate=1"],
                capture_output=True,
            )
        except OSError:
            pass
        try:
            os.chmod(dev, 0o666)
        except OSError:
            pass
        try:
            fd = os.open(dev, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as e:
            last_err = e
            time.sleep(0.15 * (attempt + 1))
            continue
        try:
            flg = fcntl.fcntl(fd, fcntl.F_GETFL)
            fcntl.fcntl(fd, fcntl.F_SETFL, flg | os.O_NONBLOCK)
        except OSError:
            pass
        if _fd_has_capture(fd):
            return fd
        os.close(fd)
        time.sleep(0.15 * (attempt + 1))
    if last_err:
        raise last_err
    raise OSError("loopback open without CAPTURE")


def _write_yuyv(fd: int, yuyv: bytes) -> int:
    try:
        return os.write(fd, yuyv)
    except BlockingIOError:
        return 0
    except OSError:
        return -1


def _holds_dev(pid_dir: Path, dev: str) -> bool:
    fd_dir = pid_dir / "fd"
    try:
        names = os.listdir(fd_dir)
    except OSError:
        return False
    real = None
    try:
        real = os.path.realpath(dev)
    except OSError:
        pass
    for name in names:
        try:
            t = os.readlink(fd_dir / name)
        except OSError:
            continue
        if t == dev or t.endswith(dev) or (real and t == real):
            return True
    return False


_LOADING: dict[tuple[int, int], bytes] = {}


def out_size(name: str) -> tuple[int, int]:
    return OUT.get(name, OUT["front"])


def loading_yuyv(name: str = "front") -> bytes:
    """Dark frame with a caption — not a frozen sensor still. exclusive_caps stays Capture."""
    ow, oh = out_size(name)
    key = (ow, oh)
    cached = _LOADING.get(key)
    if cached is not None:
        return cached
    bgr = np.full((oh, ow, 3), 18, dtype=np.uint8)
    text = "loading"
    scale = 2.0 if ow >= 960 else 0.7
    thick = 3 if ow >= 960 else 1
    (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_DUPLEX, scale, thick)
    tw = (tw + 1) & ~1
    x = ((ow - tw) // 2) & ~1
    y = (oh + th) // 2
    # no LINE_AA — antialias + YUYV 4:2:2 used to render as 'loodding'
    cv2.putText(
        bgr, text, (x, y), cv2.FONT_HERSHEY_DUPLEX, scale, (210, 210, 210), thick, cv2.LINE_8
    )
    frame = sc.bgr_or_grey_to_yuyv(bgr)
    _LOADING[key] = frame
    return frame


def load_hold_frames() -> dict[str, bytes]:
    """No last-frame cache — loading frame only (per-cam size)."""
    try:
        for p in CACHE_DIR.glob("*.yuyv"):
            p.unlink()
    except OSError:
        pass
    return {n: loading_yuyv(n) for n in ("front", "back", "ir")}


def save_hold_frame(name: str, yuyv: bytes) -> None:
    # do not persist a freeze-frame — after STREAMOFF go back to loading
    return


def pw_running_cams() -> set[str]:
    now = time.time()
    if now - _PW_CACHE["t"] < 0.4:
        return set(_PW_CACHE["running"])
    env = os.environ.copy()
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path=/run/user/{os.getuid()}/bus")
    running: set[str] = set()
    try:
        raw = subprocess.run(
            ["pw-dump"],
            capture_output=True,
            timeout=1.5,
            env=env,
        ).stdout
        if raw:
            for o in json.loads(raw):
                info = o.get("info") or {}
                p = info.get("props") or {}
                if p.get("media.class") != "Video/Source":
                    continue
                if info.get("state") != "running":
                    continue
                path = p.get("api.v4l2.path") or ""
                for key, dev in DEV.items():
                    base = dev.rsplit("/", 1)[-1]
                    if path == dev or path.endswith(base):
                        running.add(key)
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError, ValueError):
        running = set(_PW_CACHE["running"])
    _PW_CACHE["t"] = now
    _PW_CACHE["running"] = running
    return set(running)


def readers_of(dev: str) -> set[int]:
    """Anyone holding the loopback except us / v4l2-ctl — including pipewire on getUserMedia."""
    out: set[int] = set()
    me = os.getpid()
    for pid in Path("/proc").iterdir():
        if not pid.name.isdigit():
            continue
        p = int(pid.name)
        if p == me:
            continue
        try:
            comm = (pid / "comm").read_text().strip()
        except OSError:
            continue
        if comm in SKIP_COMMS:
            continue
        try:
            cmd = (pid / "cmdline").read_bytes().replace(b"\0", b" ").decode("utf-8", "ignore")
        except OSError:
            cmd = ""
        if "surface_webcamd" in cmd or "surface_webcam_daemon" in cmd:
            continue
        if _holds_dev(pid, dev):
            out.add(p)
    return out


def _cmd_looks_like_howdy(cmd: str) -> bool:
    """Match real Howdy / enroll / PAM — not harness scripts named *test-howdy-*."""
    c = cmd.lower()
    if "test-howdy-switch" in c or "test-ir-switch" in c:
        return False
    needles = (
        "pam_howdy",
        "howdy/cli",
        "/usr/bin/howdy",
        "/bin/howdy",
        "surface-howdy-add",
        "surface-howdy-hold",
        "surface-howdy-test",
        "surface-howdy-enroll",
        "surface-howdy-gate",
    )
    if any(n in c for n in needles):
        return True
    # bare `howdy` token (argv0 or path segment), not substring of unrelated words
    import re
    return re.search(r"(^|[\s/])howdy([\s./]|$)", c) is not None


def _pid_is_howdy(pid: int) -> bool:
    """True if this pid or any ancestor cmdline mentions howdy / surface-howdy."""
    seen: set[int] = set()
    cur = pid
    while cur > 1 and cur not in seen:
        seen.add(cur)
        try:
            cmd = Path(f"/proc/{cur}/cmdline").read_bytes().replace(b"\0", b" ").decode(
                "utf-8", "ignore"
            )
        except OSError:
            break
        if _cmd_looks_like_howdy(cmd):
            return True
        try:
            for line in Path(f"/proc/{cur}/status").read_text().splitlines():
                if line.startswith("PPid:"):
                    cur = int(line.split()[1])
                    break
            else:
                break
        except (OSError, ValueError, IndexError):
            break
    return False


def howdy_holding_ir() -> bool:
    """True when Howdy (or enroll) holds the IR loopback — LED blink mode."""
    ir = DEV.get("ir")
    if not ir:
        return False
    return any(_pid_is_howdy(pid) for pid in readers_of(ir))


def enforce_ir_mutex() -> str:
    """Exclusive IR: Howdy OR preview/browser — never both.

    When Howdy holds IR, drop non-Howdy readers (Chrome/Firefox/ffmpeg/apps).
    Returns owner: howdy | preview | idle.
    """
    ir = DEV.get("ir")
    if not ir:
        return "idle"
    readers = readers_of(ir)
    if not readers:
        try:
            IR_OWNER.write_text("idle\n")
        except OSError:
            pass
        return "idle"
    howdy_pids = {p for p in readers if _pid_is_howdy(p)}
    other_pids = readers - howdy_pids
    if howdy_pids:
        for pid in other_pids:
            try:
                os.kill(pid, signal.SIGTERM)
                print(f"IR mutex: dropped preview pid={pid} (Howdy owns IR)", flush=True)
            except OSError:
                pass
        owner = "howdy"
    else:
        owner = "preview"
    try:
        IR_OWNER.write_text(owner + "\n")
    except OSError:
        pass
    return owner


def pick_wanted(current: str, has_seed: bool) -> str:
    pw = pw_running_cams()
    ir = bool(readers_of(DEV["ir"]) or ("ir" in pw))
    bk = bool(readers_of(DEV["back"]) or ("back" in pw))
    fr = bool(readers_of(DEV["front"]) or ("front" in pw))
    hint = ""
    if HINT.exists():
        try:
            hint = HINT.read_text().strip()
        except OSError:
            hint = ""
        if hint not in DEV:
            hint = ""
    # Howdy / IR readers always preempt Front and Back (when IR is enabled).
    if IR_ENABLED and ir:
        return "ir"
    # HINT only tie-breaks Front/Back when someone is actually reading.
    # The hint file alone must not keep STREAMON — otherwise the LED stays on after tests.
    if fr and bk and hint in ("front", "back"):
        return hint
    if fr:
        return "front"
    if bk:
        return "back"
    return "idle"


def write_status(active: str, extra: str) -> None:
    try:
        STATUS.parent.mkdir(parents=True, exist_ok=True)
        STATUS.write_text(f"active={active} {extra}\n")
    except OSError:
        pass


class Capture:
    def __init__(self) -> None:
        self.stop = threading.Event()
        self.cap: subprocess.Popen | None = None
        self.name = "front"
        self.last_yuyv: dict[str, bytes | None] = {"front": None, "back": None, "ir": None}
        self.stats = {"got": 0, "drop": 0, "dec": 0, "out": 0}
        self._threads: list[threading.Thread] = []
        self._from_back = False
        self._front_retries = 0
        self._hold_only = False
        self._backoff_until = 0.0
        self._front_sterile = False
        self._need_cycle = False
        self._ir_led_stop = threading.Event()
        self._ir_led_thread: threading.Thread | None = None

    def _stop_ir_led(self) -> None:
        self._ir_led_stop.set()
        if self._ir_led_thread and self._ir_led_thread.is_alive():
            self._ir_led_thread.join(timeout=1.0)
        self._ir_led_thread = None
        sc.ir_led(False)
        try:
            IR_LED_MODE.write_text("off\n")
        except OSError:
            pass

    def _start_ir_led(self) -> None:
        """Arm IR flood once after STREAMON — long strobe span (looks continuous).

        Do not re-run ir-led-on in a loop: rewriting I2C mid-stream causes visible flicker.
        """
        self._stop_ir_led()
        self._ir_led_stop.clear()
        sc.ir_led(True)
        try:
            IR_LED_MODE.write_text("steady\n")
        except OSError:
            pass

        def _run() -> None:
            # Status only — LED stays armed until halt()
            while not self._ir_led_stop.is_set():
                try:
                    mode = "howdy-steady" if howdy_holding_ir() else "steady"
                    IR_LED_MODE.write_text(mode + "\n")
                except OSError:
                    pass
                if self._ir_led_stop.wait(5.0):
                    break

        self._ir_led_thread = threading.Thread(target=_run, daemon=True, name="ir-led")
        self._ir_led_thread.start()

    def start(self, name: str) -> None:
        reap_orphan_holds(keep_pid=self.cap.pid if self.cap else None)
        ok, msg = ig.health_check()
        if not ok:
            raise RuntimeError(f"IPU unhealthy: {msg}")
        cam = sc.CAMS[name]
        cfg = CAP[name]
        sensor = cfg["sensor"]
        for other in ("ov5693", "ov8865", "ov7251"):
            if other == sensor:
                continue
            sc._sensor_pm_write(other, "auto")
            sc.sensor_runtime_cycle(other, timeout=6.0)
        # Like Windows: ISYS must sleep between cameras (~2–3 s),
        # then isys_setup_hw rearms AFE from scratch. Do not block Front.
        # IR: do NOT suspend ISYS before Front-warm→IR — that cold-starts CSI and yields 0 DQBUF.
        if name == "ir":
            print(f"IR: skip switch-cycle (isys={isys_runtime()})", flush=True)
            self._front_sterile = False
        elif name == "front" or self._from_back or self._need_cycle or self._front_sterile:
            print(f"switch-cycle before {name} (isys={isys_runtime()})", flush=True)
            wait_switch_cycle()
            self._front_sterile = False
        self._from_back = False
        self._need_cycle = False
        if name == "front" and self._front_retries:
            print(f"Front retry {self._front_retries} after ISYS cycle", flush=True)
        sc._sensor_pm_write(sensor, "auto")
        if not sc.sensor_runtime_cycle(sensor, timeout=3.0):
            sc.run(["media-ctl", "-d", "/dev/media0", "-r"], check=False)
            sc._sensor_pm_write(sensor, "auto")
            if not sc.sensor_runtime_cycle(sensor, timeout=8.0):
                raise RuntimeError(
                    f"{sensor} not suspended ({sc.sensor_runtime_status(sensor)})"
                )
        # IR: Front warm → ONE STREAMON @ Windows ConfigMipiClk layout
        # (+0x34=1155, +0x3c=1269). Do not STREAMOFF 0-DQBUF / all-0xFF.
        if name == "ir":
            try:
                spid = int(Path("/run/surface-ipu/ir-sterile-hold.pid").read_text().strip())
                if Path(f"/proc/{spid}").exists():
                    raise RuntimeError(
                        f"IR sterile hold pid={spid} still STREAMON — power cycle needed"
                    )
            except (OSError, ValueError):
                pass
            try:
                print("IR warm: CAMS=front surface-cam-capture", flush=True)
                warm = subprocess.run(
                    ["/usr/local/bin/surface-cam-capture"],
                    env={**os.environ, "CAMS": "front", "OUT": "/tmp/camtest-ir-warm"},
                    timeout=20,
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                time.sleep(0.5)
                print(f"IR Front-warm done rc={warm.returncode}", flush=True)
            except Exception as e:
                print(f"IR Front-warm skipped: {e}", flush=True)
            Path("/sys/module/intel_ipu4p_isys/parameters/sb3_ir_clk_ticks").write_text("1155")
            Path("/sys/module/intel_ipu4p_isys/parameters/sb3_ir_data_ticks").write_text("1269")
            print("IR STREAMON @ Windows +0x34=1155 +0x3c=1269", flush=True)

        if name == "ir":
            Path("/sys/module/intel_ipu4p_isys/parameters/sb3_ir_clk_ticks").write_text("1155")
            Path("/sys/module/intel_ipu4p_isys/parameters/sb3_ir_data_ticks").write_text("1269")
        # Refuse new STREAMON if ISYS already EIO (avoids deeper hang)
        if name in ("ir", "front"):
            try:
                fd = os.open(
                    "/dev/video5" if name == "ir" else "/dev/video10",
                    os.O_RDWR | os.O_NONBLOCK,
                )
                os.close(fd)
            except OSError as e:
                Path("/run/surface-ipu/isys-dead").write_text(
                    f"{time.strftime('%Y-%m-%dT%H:%M:%S')} open fail {e}\n"
                )
                raise RuntimeError(f"ISYS dead before {name} STREAMON: {e}") from e
        dev = sc.media_setup(cam, cfg["w"], cfg["h"])
        sc.set_sensor_exposure(sensor, cam.exposure, cam.gain)
        if name == "back":
            self._front_retries = 0
            try:
                BACK_USED.unlink(missing_ok=True)
            except OSError:
                pass
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
        if name == "ir":
            # Extra blanking gives headroom for strobe span ≈ exposure (steadier LED).
            sub = sc.find_subdev("ov7251")
            if sub:
                subprocess.run(
                    [
                        "v4l2-ctl",
                        "-d",
                        sub,
                        "--set-ctrl",
                        f"exposure={cam.exposure},analogue_gain={cam.gain},vertical_blanking=400",
                    ],
                    capture_output=True,
                )
        stride = sc.get_stride(dev, cfg["w"])
        # raw_hold writes bytesused-data_offset (typically stride*h).
        # stride*h-4 desynchronized Bayer (red maze / stripe).
        fl = stride * cfg["h"]
        print(
            f"capture {name} {dev} {cfg['w']}x{cfg['h']} stride={stride} fl={fl}",
            flush=True,
        )
        self.stop.clear()
        self.name = name
        latest_raw: list[bytes | None] = [None]
        self.stats = {"got": 0, "drop": 0, "dec": 0, "out": self.stats.get("out", 0)}
        # raw_hold: DQBUF EIO does not STREAMOFF (v4l2-ctl exits after ~2 s and kills ISYS).
        hold = os.path.join(ROOT, "tools", "raw_hold")
        fourcc = cam.fourcc if len(cam.fourcc) == 4 else "pBAA"
        v4l_cmd = [hold, dev, str(cfg["w"]), str(cfg["h"]), fourcc]
        err_path = Path("/run/surface-webcam") / f"raw_hold-{name}.err"
        try:
            err_f = err_path.open("wb")
        except OSError:
            err_f = subprocess.DEVNULL
        self.cap = subprocess.Popen(
            v4l_cmd,
            stdout=subprocess.PIPE,
            stderr=err_f,
        )
        if err_f is not subprocess.DEVNULL:
            try:
                err_f.close()
            except OSError:
                pass
        assert self.cap.stdout
        try:
            fcntl.fcntl(self.cap.stdout.fileno(), fcntl.F_SETPIPE_SZ, 1 << 21)
        except OSError:
            pass
        os.set_blocking(self.cap.stdout.fileno(), False)
        tone = LockedTone()

        def _reader() -> None:
            buf = bytearray()
            while not self.stop.is_set() and self.cap and self.cap.stdout:
                try:
                    chunk = self.cap.stdout.read(1 << 20)
                except BlockingIOError:
                    chunk = b""
                if not chunk:
                    if self.cap.poll() is not None:
                        err = b""
                        if self.cap.stderr:
                            try:
                                err = self.cap.stderr.read() or b""
                            except OSError:
                                err = b""
                        print(
                            "raw_hold died",
                            self.cap.returncode,
                            err.decode("utf-8", "ignore").strip()[:200],
                            flush=True,
                        )
                        self.stop.set()
                        return
                    time.sleep(0.001)
                    continue
                buf.extend(chunk)
                extra = len(buf) // fl
                if extra == 0:
                    continue
                if extra > 1:
                    self.stats["drop"] += extra - 1
                raw = bytes(buf[(extra - 1) * fl : extra * fl])
                del buf[: extra * fl]
                latest_raw[0] = raw
                self.stats["got"] += extra
                got = self.stats["got"]
                if got <= extra or got in (20, 40):
                    try:
                        dump = Path("/run/surface-webcam") / f"raw-{name}-{got}.bin"
                        dump.write_bytes(raw)
                        loff = detect_line_off(raw)
                        frac = raw_good_line_frac(raw, cfg["h"], stride, loff)
                        img = sc.unpack_mipi10_u16(
                            raw, cfg["w"], cfg["h"], stride, line_off=loff
                        )
                        print(
                            f"RAW {name} n={got} len={len(raw)} off={loff} "
                            f"good={frac:.2f} head={raw[:8].hex()} "
                            f"p1={np.percentile(img,1):.0f} "
                            f"p50={np.percentile(img,50):.0f} "
                            f"p99={np.percentile(img,99):.0f} "
                            f"mean={float(img.mean()):.1f}]",
                            flush=True,
                        )
                    except Exception as e:
                        print(f"RAW dump fail {e}", flush=True)

        def _decode() -> None:
            last = None
            ae_n = 0
            ae_exp = int(cam.exposure)
            ae_gain = int(cam.gain)
            ae_last_t = 0.0
            while not self.stop.is_set():
                raw = latest_raw[0]
                if raw is None or raw is last:
                    time.sleep(0.002)
                    continue
                last = raw
                # IR: first seconds after STREAMON are often 0xFF; keep STREAMON, skip decode.
                if name == "ir":
                    sample = raw[::64]
                    if sample and (sum(sample) / len(sample)) > 240:
                        continue
                bgr = decode(
                    cam,
                    raw,
                    cfg["w"],
                    cfg["h"],
                    stride,
                    tone,
                    cfg["crop_r"],
                    cfg["crop_b"],
                    cfg.get("crop_l", 0),
                )
                if bgr is None:
                    continue
                # Drop near-black IR frames — hold last good YUYV (stops black flashes).
                if name == "ir":
                    ow, oh = out_size(name)
                    expect = ow * oh * 2
                    mean = float(np.mean(bgr))
                    prev_y = self.last_yuyv.get(name)
                    if mean < 8.0 and prev_y is not None and len(prev_y) == expect:
                        continue
                    if bgr.shape[0] != oh or bgr.shape[1] != ow:
                        # Should already be 480x640; refuse wrong geometry.
                        continue
                # Slow IR AE only — hunting every few frames looked like LED blink.
                if name == "ir":
                    ae_n += 1
                    now = time.monotonic()
                    if ae_n >= 45 and now - ae_last_t >= 1.2:
                        ae_last_t = now
                        try:
                            img16 = sc.unpack_mipi10_u16(
                                raw, cfg["w"], cfg["h"], stride, line_off=4
                            )
                            hh, ww = img16.shape
                            crop = img16[hh // 5 : (4 * hh) // 5, ww // 5 : (4 * ww) // 5]
                            p95 = float(np.percentile(crop, 95))
                            sat = float((crop > 900).mean())
                            if p95 >= 80:  # ignore sterile/dark alternate frames
                                if sat > 0.015 or p95 > 700:
                                    new_exp = max(35, ae_exp - 15)
                                    new_gain = max(8, ae_gain - 2) if ae_exp <= 45 else ae_gain
                                elif p95 < 220:
                                    new_exp = min(160, ae_exp + 8)
                                    new_gain = ae_gain
                                else:
                                    new_exp, new_gain = ae_exp, ae_gain
                                if new_exp != ae_exp or new_gain != ae_gain:
                                    sc.set_sensor_exposure("ov7251", new_exp, new_gain)
                                    ae_exp, ae_gain = new_exp, new_gain
                                    cam.exposure, cam.gain = new_exp, new_gain
                                    sc.ir_led(True)
                                    print(
                                        f"IR AE slow exp={ae_exp} gain={ae_gain} "
                                        f"p95={p95:.0f} sat={sat:.3f}",
                                        flush=True,
                                    )
                        except Exception as e:
                            print(f"IR AE skip: {e}", flush=True)
                yuyv = sc.bgr_or_grey_to_yuyv(bgr)
                self.last_yuyv[name] = yuyv
                self.stats["dec"] += 1

        self._threads = [
            threading.Thread(target=_reader, daemon=True, name=f"cap-{name}"),
            threading.Thread(target=_decode, daemon=True, name=f"dec-{name}"),
        ]
        for t in self._threads:
            t.start()
        if name == "ir":
            # ir-led-on: long span after STREAMON (mode table has short blink pulse)
            self._start_ir_led()
            # Mode table / s_stream can overwrite span once — re-arm after first frames
            def _rear_led() -> None:
                time.sleep(0.6)
                if not self.stop.is_set() and self.name == "ir":
                    sc.ir_led(True)
                    print("IR LED re-armed (span≈exposure)", flush=True)

            threading.Thread(target=_rear_led, daemon=True, name="ir-led-rear").start()

    def halt(self, force: bool = False) -> bool:
        """Clean STREAMOFF (TERM, never SIGKILL). False = do not start the next STREAMON.

        force=True also refuses STREAMOFF of 0-frame Front — that kills ISYS.
        """
        prev = self.name
        if prev not in CAP and self.cap is None:
            return True
        already_off = self.cap is not None and self.cap.poll() is not None
        # 0-frame STREAMOFF on Front/IR kills ISYS.
        # All-0xFF IR (dec=0) = DPHY never locked — STREAMOFF also kills ISYS (2026-09-28).
        zero_stream = (
            prev in ("front", "ir")
            and self.stats.get("got", 0) < 5
            and self.cap is not None
            and not already_off
        )
        ir_unlocked_ff = (
            prev == "ir"
            and self.stats.get("got", 0) >= 5
            and self.stats.get("dec", 0) == 0
            and self.cap is not None
            and not already_off
        )
        if zero_stream or ir_unlocked_ff:
            why = "0-frame" if zero_stream else "all-0xFF (dec=0)"
            print(
                f"refuse STREAMOFF of {why} {prev} — that kills ISYS",
                flush=True,
            )
            try:
                Path("/run/surface-ipu").mkdir(parents=True, exist_ok=True)
                if self.cap and self.cap.pid:
                    Path("/run/surface-ipu/ir-sterile-hold.pid").write_text(
                        f"{self.cap.pid}\n"
                    )
            except OSError:
                pass
            return False
        skip_health = False
        if already_off and prev == "front" and self.stats.get("dec", 0) == 0:
            print("Front v4l2-ctl already dead — STREAMOFF done, cleaning up", flush=True)
            skip_health = True
        self.stop.set()
        cap = self.cap
        self.cap = None
        if cap and cap.poll() is None:
            try:
                cap.send_signal(signal.SIGTERM)
            except OSError:
                pass
            try:
                cap.wait(timeout=6.0)
            except subprocess.TimeoutExpired:
                print("STREAMOFF timeout — leave IPU alone", flush=True)
                set_privacy_led(prev, False)
                return False
        for t in self._threads:
            t.join(timeout=1.0)
        self._threads = []
        if prev not in CAP:
            ok, msg = ig.health_check()
            return ok
        sensor = CAP[prev]["sensor"]
        sc._sensor_pm_write(sensor, "auto")
        sc.sensor_runtime_cycle(sensor, timeout=6.0)
        set_privacy_led(prev, False)
        if prev == "ir":
            self._stop_ir_led()
            self._need_cycle = True
        elif prev == "back":
            self._from_back = True
            self._need_cycle = True
            time.sleep(0.3)
        elif prev == "front":
            self._need_cycle = True
        if prev in self.last_yuyv:
            self.last_yuyv[prev] = loading_yuyv(prev)
        if skip_health:
            return True
        ok, msg = ig.health_check()
        if not ok:
            print(f"health after STREAMOFF: {msg}", flush=True)
            return False
        return True


def reap_orphan_holds(keep_pid: int | None = None) -> None:
    """KillMode=process can leave raw_hold on video10 — ov5693 then never suspends.

    Never TERM IR (video5 / Y10) holds: STREAMOFF of 0-DQBUF kills ISYS.
    """
    me = os.getpid()
    protect: set[int] = set()
    if keep_pid:
        protect.add(keep_pid)
    try:
        protect.add(int(Path("/run/surface-ipu/ir-sterile-hold.pid").read_text().strip()))
    except (OSError, ValueError):
        pass
    try:
        protect.add(int(Path("/run/surface-ipu/ir-hold.pid").read_text().strip()))
    except (OSError, ValueError):
        pass
    for ent in Path("/proc").iterdir():
        if not ent.name.isdigit():
            continue
        pid = int(ent.name)
        if pid in (me, *protect):
            continue
        try:
            cmd = (ent / "cmdline").read_bytes().replace(b"\0", b" ").decode("utf-8", "ignore")
        except OSError:
            continue
        exe = cmd.split()[0] if cmd.strip() else ""
        if os.path.basename(exe) != "raw_hold":
            continue
        if "/dev/video" not in cmd:
            continue
        # IR capture node — leave STREAMON forever if sterile
        if "/dev/video5" in cmd or "Y10" in cmd:
            print(f"keep IR raw_hold pid={pid} (no STREAMOFF)", flush=True)
            continue
        print(f"TERM orphan raw_hold pid={pid} {cmd[:120]}", flush=True)
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            continue
    time.sleep(0.4)


def main() -> None:
    try:
        os.makedirs("/run/surface-webcam", exist_ok=True)
    except OSError:
        pass
    global DEV
    DEV.update(resolve_loopbacks())
    print(f"loopbacks front={DEV['front']} back={DEV['back']} ir={DEV['ir']}", flush=True)
    try:
        BACK_USED.unlink(missing_ok=True)
    except OSError:
        pass
    try:
        HINT.unlink(missing_ok=True)
    except OSError:
        pass
    reap_orphan_holds()
    ok, msg = ig.health_check()
    if not ok:
        print(f"IPU unhealthy at start — loading, no STREAMON: {msg}", flush=True)

    fds: dict[str, int] = {}
    for name, dev in DEV.items():
        ow, oh = out_size(name)
        fds[name] = _open_loopback(dev, ow, oh)
        print(f"loopback {name} {dev} {ow}x{oh}", flush=True)

    cap = Capture()
    cap.name = "idle"
    cap.last_yuyv.update(load_hold_frames())
    last_switch = time.monotonic()
    last_front_rd = 0.0
    last_back_rd = 0.0
    last_ir_rd = 0.0
    stop = threading.Event()
    if not ok:
        cap._hold_only = True
        cap._backoff_until = time.monotonic() + DEAD_BACKOFF
        print(f"IPU unhealthy — hold-only, no STREAMON: {msg}", flush=True)

    tick = {"n": 0}

    def _reopen(name: str) -> None:
        old = fds.get(name, -1)
        if old >= 0:
            try:
                os.close(old)
            except OSError:
                pass
        DEV.update(resolve_loopbacks())
        try:
            ow, oh = out_size(name)
            fds[name] = _open_loopback(DEV[name], ow, oh)
            print(f"loopback writer {name} {DEV[name]} {ow}x{oh}", flush=True)
        except OSError as e:
            fds[name] = -1
            print(f"loopback reopen {name} {e}", flush=True)

    def _cfr() -> None:
        period = 1.0 / FPS
        last_full = {n: time.monotonic() for n in DEV}
        while not stop.is_set():
            t0 = time.time()
            tick["n"] += 1
            for name in list(DEV):
                fd = fds.get(name, -1)
                if fd < 0:
                    _reopen(name)
                    continue
                # guvcview kill: writer dies, exclusive_caps → OUTPUT-only, EAGAIN forever
                if tick["n"] % 30 == 0 and not _fd_has_capture(fd):
                    print(f"loopback {name} lost CAPTURE — reopen", flush=True)
                    _reopen(name)
                    last_full[name] = time.monotonic()
                    continue
                yuyv = cap.last_yuyv.get(name) or loading_yuyv(name)
                ow, oh = out_size(name)
                expect = ow * oh * 2
                if len(yuyv) != expect:
                    yuyv = loading_yuyv(name)
                n = _write_yuyv(fd, yuyv)
                if n == len(yuyv):
                    last_full[name] = time.monotonic()
                    cap.stats["out"] += 1
                elif n < 0 or (time.monotonic() - last_full[name] > 1.5):
                    _reopen(name)
                    last_full[name] = time.monotonic()
            time.sleep(max(0.0, period - (time.time() - t0)))

    threading.Thread(target=_cfr, daemon=True, name="cfr").start()

    def _die(*_a) -> None:
        stop.set()
        cap.stop.set()

    signal.signal(signal.SIGTERM, _die)
    signal.signal(signal.SIGINT, _die)

    t_log = time.monotonic()
    n_log = 0
    print(
        f"webcamd up idle (LED off, IR={'on' if IR_ENABLED else 'off'}, "
        "loopback seeded so Chrome lists cameras)",
        flush=True,
    )
    while not stop.is_set():
        now = time.monotonic()
        if now - t_log >= 2.0:
            dec = cap.stats["dec"]
            inst = (dec - n_log) / max(now - t_log, 1e-6)
            owner = enforce_ir_mutex()
            led_mode = "off"
            try:
                led_mode = IR_LED_MODE.read_text().strip() or "off"
            except OSError:
                pass
            extra = (
                f"n={dec} unique_fps={inst:.1f} drop={cap.stats['drop']} "
                f"out={cap.stats['out']} cam={cap.name} "
                f"ir_owner={owner} ir_led={led_mode} "
                f"rf={sorted(readers_of(DEV['front']))} rb={sorted(readers_of(DEV['back']))} "
                f"ri={sorted(readers_of(DEV['ir']))} "
                f"pw={sorted(pw_running_cams())}"
            )
            print(f"LIVE {extra}", flush=True)
            write_status(cap.name, extra)
            t_log = now
            n_log = dec
        has_seed = any(cap.last_yuyv.get(n) for n in DEV)
        pw = pw_running_cams()
        if readers_of(DEV["front"]) or "front" in pw:
            last_front_rd = now
        if readers_of(DEV["back"]) or "back" in pw:
            last_back_rd = now
        if readers_of(DEV["ir"]) or "ir" in pw:
            last_ir_rd = now
        # Mutex check every loop tick while IR is in play
        if IR_ENABLED and (readers_of(DEV["ir"]) or "ir" in pw or cap.name == "ir"):
            enforce_ir_mutex()
        if cap._hold_only:
            time.sleep(0.25)
            continue
        hold = IDLE_SWITCH if cap.name == "idle" else SWITCH_HOLD
        if cap.name == "front" and cap.stats["dec"] == 0:
            hold = FRONT_LOCK_HOLD
        if now - last_switch >= hold:
            want = pick_wanted(cap.name, has_seed)
            # Chrome/PipeWire drops the fd briefly — do not idle mid-session.
            if now - last_ir_rd < LINGER_S and IR_ENABLED and cap.stats.get("dec", 0) > 0:
                want = "ir"
            elif (
                now - last_front_rd < LINGER_S
                and not cap._front_sterile
                and cap.stats.get("dec", 0) > 0
                and want != "ir"
            ):
                want = "front"
            elif now - last_back_rd < LINGER_S and cap.stats.get("dec", 0) > 0 and want != "ir":
                want = "back"
            # STREAMON is live with zero frames — do NOT STREAMOFF a live Front (kills ISYS).
            if (
                want == "front"
                and cap.name == "front"
                and cap.stats.get("dec", 0) == 0
            ):
                dead = cap.cap is None or cap.cap.poll() is not None
                if dead:
                    print(
                        "Front STREAMON died — cleanup, backoff, no storm",
                        flush=True,
                    )
                    cap.halt(force=True)
                    cap.name = "idle"
                    cap._backoff_until = time.monotonic() + FAIL_BACKOFF
                    last_switch = time.monotonic()
                    continue
                print(
                    "Front 0 frames after lock — keep STREAMON, no STREAMOFF",
                    flush=True,
                )
                last_switch = time.monotonic()
                continue
            # IR: 0xFF frames still increment got — keep STREAMON (decode skips them).
            # Only sterile if zero DQBUF for a long time or raw_hold died.
            if cap.name == "ir" and cap.stats.get("dec", 0) == 0:
                dead = cap.cap is None or cap.cap.poll() is not None
                age = time.monotonic() - last_switch
                got = cap.stats.get("got", 0)
                if dead:
                    print("IR raw_hold died — halt/backoff", flush=True)
                    if not cap.halt(force=True):
                        print("IR hold refused STREAMOFF — stay hold-only", flush=True)
                        cap._hold_only = True
                        cap._backoff_until = time.monotonic() + 3600
                        last_switch = time.monotonic()
                        continue
                    cap.name = "idle"
                    cap._backoff_until = time.monotonic() + FAIL_BACKOFF
                    last_switch = time.monotonic()
                    continue
                if got == 0 and age > 12.0:
                    print(
                        "IR STREAMON sterile (0 DQBUF 12s) — refuse STREAMOFF, hold-only",
                        flush=True,
                    )
                    if not cap.halt(force=True):
                        print(
                            "idle blocked — keep STREAMON and name="
                            f"{cap.name} (sterile hold)",
                            flush=True,
                        )
                        cap._hold_only = True
                        cap._backoff_until = time.monotonic() + 3600
                        last_switch = time.monotonic()
                        continue
                    # halt allowed STREAMOFF (unexpected) — still back off
                    cap.name = "idle"
                    cap._backoff_until = time.monotonic() + FAIL_BACKOFF
                    last_switch = time.monotonic()
                    continue
                if got > 0 and age > 2.0 and age < 2.5:
                    print(f"IR locked DQBUF got={got} (waiting non-0xFF decode)", flush=True)
            if want == cap.name:
                pass
            elif want == "idle":
                print(f"idle (LED off) seed={has_seed}", flush=True)
                if cap.name != "idle":
                    if not cap.halt(force=True):
                        print(
                            "idle blocked — keep STREAMON and name="
                            f"{cap.name}",
                            flush=True,
                        )
                        # all-0xFF / 0-frame IR: stop idle storm, wait for reboot
                        if cap.name == "ir" and cap.stats.get("dec", 0) == 0:
                            cap._hold_only = True
                            cap._backoff_until = time.monotonic() + 3600
                            print("IR hold-only (dec=0) — no idle storm", flush=True)
                        last_switch = time.monotonic()
                        continue
                    cap.name = "idle"
                set_privacy_led("front", False)
                set_privacy_led("back", False)
                cap._stop_ir_led()
                # after a fast guvcview close reopen the writer — Back back on the camera list
                for lb in DEV:
                    _reopen(lb)
                last_switch = time.monotonic()
            else:
                if now < cap._backoff_until:
                    continue
                print(
                    f"switch {cap.name} → {want} readers f={sorted(readers_of(DEV['front']))} "
                    f"b={sorted(readers_of(DEV['back']))} i={sorted(readers_of(DEV['ir']))}",
                    flush=True,
                )
                if cap.name != "idle" and not cap.halt():
                    print("switch aborted (ISYS)", flush=True)
                    last_switch = time.monotonic()
                    continue
                try:
                    cap.start(want)
                    last_switch = time.monotonic()
                except Exception as e:
                    print(f"switch start {want} failed: {e}", flush=True)
                    err = str(e).lower()
                    if "unhealthy" in err or "dead" in err or "hang" in err:
                        cap._hold_only = True
                        cap._backoff_until = time.monotonic() + DEAD_BACKOFF
                        print(
                            f"ISYS dead — hold-only {DEAD_BACKOFF:.0f}s, no storm",
                            flush=True,
                        )
                    else:
                        cap._backoff_until = time.monotonic() + FAIL_BACKOFF
                    last_switch = time.monotonic()
        time.sleep(0.25)

    cap.halt(force=True)
    set_privacy_led("front", False)
    set_privacy_led("back", False)
    cap._stop_ir_led()
    for fd in fds.values():
        try:
            os.close(fd)
        except OSError:
            pass


if __name__ == "__main__":
    main()
