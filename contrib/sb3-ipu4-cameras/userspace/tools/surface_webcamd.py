#!/usr/bin/env python3
"""Always-on Surface Book 3 webcams — live frames, no placeholders, no terminal.

IPU4 = one sensor at a time. Front Standard is the default camera.
Resolve loopbacks by card name (7 devices):
  Surface-Front-{Standard,HQ,Fast}, Surface-Back-{Standard,HQ,Fast}, Surface-IR-Howdy
(/dev/video* numbers may be 60–66 or swapped).
One physical STREAMON at a time; opening any profile preempts the rest.
IR LED: continuous ON while IR streams. Status tracks howdy vs preview.

SURFACE_WEBCAM_IR=1 enables IR (required for Howdy). Set 0 to keep IR off.
"""
from __future__ import annotations

import fcntl
import errno
import json
import os
import shutil
import signal
import stat
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

_BOOT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_TREE_ROOT = "/home/popos/Pulpit/cursor/surface-cameras"
_OPT_ROOT = "/opt/surface-cameras"


def _vendor_paths_for(py: tuple[int, int] | None = None) -> list[str]:
    """ABI-matched OpenCV/numpy dirs. Never mix vendor/ (cp314) with vendor-py315."""
    maj, min_ = py or sys.version_info[:2]
    tag = "vendor-py315" if (maj, min_) >= (3, 15) else "vendor"
    out: list[str] = []
    for root in (_BOOT_ROOT, _TREE_ROOT, _OPT_ROOT):
        out.append(os.path.join(root, tag))
        out.append(os.path.join(root, "lib"))
    out.append(f"/home/popos/.local/lib/python{maj}.{min_}/site-packages")
    # Unique, existing only
    seen: set[str] = set()
    uniq: list[str] = []
    for p in out:
        if p and p not in seen and os.path.isdir(p):
            seen.add(p)
            uniq.append(p)
    return uniq


def _drop_other_abi_paths(py: tuple[int, int] | None = None) -> None:
    maj, min_ = py or sys.version_info[:2]
    bad = "vendor-py315" if (maj, min_) < (3, 15) else "/vendor"
    # Keep vendor-py315 on 3.15; drop bare .../vendor on 3.15.
    cleaned: list[str] = []
    for p in sys.path:
        if not p:
            cleaned.append(p)
            continue
        norm = p.rstrip("/")
        if (maj, min_) >= (3, 15):
            if norm.endswith("/vendor") and not norm.endswith("vendor-py315"):
                continue
        else:
            if "vendor-py315" in norm:
                continue
        cleaned.append(p)
    sys.path[:] = cleaned
    _ = bad


def _apply_runtime_path(py: tuple[int, int] | None = None) -> list[str]:
    wanted = _vendor_paths_for(py)
    _drop_other_abi_paths(py)
    # systemd/PYTHONPATH often still lists vendor/ (cp314). Drop it on 3.15+.
    maj, min_ = py or sys.version_info[:2]
    extra: list[str] = []
    for p in os.environ.get("PYTHONPATH", "").split(os.pathsep):
        if not p:
            continue
        norm = p.rstrip("/")
        if (maj, min_) >= (3, 15) and norm.endswith("/vendor") and not norm.endswith(
            "vendor-py315"
        ):
            continue
        if (maj, min_) < (3, 15) and "vendor-py315" in norm:
            continue
        extra.append(p)
    for p in reversed(wanted + extra):
        while p in sys.path:
            sys.path.remove(p)
        sys.path.insert(0, p)
    os.environ["PYTHONPATH"] = os.pathsep.join(wanted)
    return wanted


def _probe_cv(exe: str, py: tuple[int, int]) -> bool:
    if not exe or not os.path.isfile(exe) or not os.access(exe, os.X_OK):
        return False
    env = os.environ.copy()
    env["PYTHONPATH"] = os.pathsep.join(_vendor_paths_for(py))
    env["SURFACE_WEBCAM_NO_REEXEC"] = "1"
    try:
        r = subprocess.run(
            [exe, "-c", "import numpy, cv2"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=12,
            check=False,
        )
        return r.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


_apply_runtime_path()

# Prefer 3.15 + OpenCV5 when that interpreter can import numpy/cv2; else stay.
if os.environ.get("SURFACE_WEBCAM_NO_REEXEC") != "1":
    here = os.path.abspath(__file__)
    if sys.version_info < (3, 15):
        for _py in (
            os.environ.get("SURFACE_PYTHON315", ""),
            "/usr/bin/python3.15",
            str(Path.home() / ".local/bin/python3.15"),
        ):
            if _probe_cv(_py, (3, 15)):
                env = os.environ.copy()
                env["SURFACE_WEBCAM_NO_REEXEC"] = "1"
                env["PYTHONPATH"] = os.pathsep.join(_vendor_paths_for((3, 15)))
                os.execve(_py, [_py, "-u", here, *sys.argv[1:]], env)
    elif sys.version_info >= (3, 15):
        # 3.15 selected but wheels missing → fall back to 3.14 / python3.
        try:
            import numpy as _np_probe  # noqa: F401
            import cv2 as _cv_probe  # noqa: F401
        except Exception:
            for _py in ("/usr/bin/python3.14", "/usr/bin/python3"):
                if os.path.realpath(_py) == os.path.realpath(sys.executable):
                    continue
                if _probe_cv(_py, (3, 14)):
                    env = os.environ.copy()
                    env["SURFACE_WEBCAM_NO_REEXEC"] = "1"
                    env["PYTHONPATH"] = os.pathsep.join(_vendor_paths_for((3, 14)))
                    os.execve(_py, [_py, "-u", here, *sys.argv[1:]], env)
                    break

# CPython 3.14+/3.15+ + OpenCV5 feature gates (eager fallback on older).
try:
    from pycompat import CV2_V5, MODERN_PY, PY315_PLUS  # noqa: F401
except ImportError:  # pragma: no cover
    CV2_V5 = False
    MODERN_PY = sys.version_info >= (3, 14)
    PY315_PLUS = sys.version_info >= (3, 15)

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "lib"))
import ipu_guard as ig  # noqa: E402
import surface_cam as sc  # noqa: E402

print(
    f"runtime py={sys.version.split()[0]} cv2={getattr(cv2, '__version__', '?')} "
    f"cv5={CV2_V5} root={ROOT}",
    flush=True,
)

# SHM raw_hold path — /opt binary is often stale (pipe-only) while webcamd expects
# 8-byte headers. Self-heal by copying a SHM-capable binary into /dev/shm.
_SHM_HOLD = "/dev/shm/surface-raw_hold"
_TREE_HOLD = "/home/popos/Pulpit/cursor/surface-cameras/tools/raw_hold"


def ensure_raw_hold() -> str:
    """Return path to a SHM-capable raw_hold (always prefer /dev/shm copy)."""
    env = os.environ.get("SURFACE_RAW_HOLD", "").strip()
    srcs = [
        p
        for p in (
            env,
            _TREE_HOLD,
            os.path.join(ROOT, "tools", "raw_hold"),
            "/opt/surface-cameras/tools/raw_hold",
        )
        if p and os.path.isfile(p) and os.access(p, os.X_OK)
    ]
    if not srcs:
        raise FileNotFoundError("raw_hold binary missing")

    def _supports_shm(path: str) -> bool:
        try:
            with open(path, "rb") as f:
                return b"SURFACE_RAW_SHM" in f.read()
        except OSError:
            return False

    shm_srcs = [p for p in srcs if _supports_shm(p)]
    src = max(shm_srcs or srcs, key=lambda p: os.path.getmtime(p))
    dest = _SHM_HOLD
    # Prefer an already-good /dev/shm binary even if tree is slightly newer —
    # overwriting a running binary hits ETXTBSY and would force tree path.
    if os.path.isfile(dest) and os.access(dest, os.X_OK) and _supports_shm(dest):
        try:
            if os.path.getmtime(src) <= os.path.getmtime(dest) + 1.0:
                return dest
        except OSError:
            pass
    try:
        tmp = dest + ".new"
        shutil.copy2(src, tmp)
        os.chmod(tmp, 0o755)
        os.replace(tmp, dest)
        print(f"raw_hold deploy {src} → {dest} shm={_supports_shm(dest)}", flush=True)
        return dest
    except OSError as e:
        if os.path.isfile(dest) and _supports_shm(dest):
            print(f"raw_hold deploy skip ({e}); using existing {dest}", flush=True)
            return dest
        print(f"raw_hold shm deploy skip: {e}; using {src}", flush=True)
        return src


# Howdy commit: default ON when env unset; explicit 0/false disables.
_ir_env = os.environ.get("SURFACE_WEBCAM_IR", "1")
IR_ENABLED = _ir_env not in ("0", "false", "False", "")

# SP7-style profiles: Standard/HQ = native 4:3; Fast = center-crop 16:9.
# Front Standard is default (lowest video_nr / highest WirePlumber priority).
PROFILES: dict[str, dict] = {
    "front-standard": {
        "label": "Surface-Front-Standard",
        "cam": "front",
        "sensor": "ov5693",
        "cap_w": 1296,
        "cap_h": 972,
        "out_w": 1296,
        "out_h": 972,
        "fps": 30,
        "crop_mode": "native",
        "crop_r": 16,
        "crop_b": 12,
        "crop_l": 8,
        "prio": 20,
    },
    "front-hq": {
        "label": "Surface-Front-HQ",
        "cam": "front",
        "sensor": "ov5693",
        "cap_w": 2592,
        "cap_h": 1944,
        "out_w": 1920,
        "out_h": 1440,
        "fps": 15,
        "crop_mode": "native",
        "crop_r": 16,
        "crop_b": 12,
        "crop_l": 8,
        "prio": 30,
    },
    "front-fast": {
        "label": "Surface-Front-Fast",
        "cam": "front",
        "sensor": "ov5693",
        "cap_w": 1296,
        "cap_h": 972,
        "out_w": 1296,
        "out_h": 728,
        "fps": 60,
        "crop_mode": "crop",
        "crop_r": 16,
        "crop_b": 12,
        "crop_l": 8,
        "prio": 10,
    },
    "back-standard": {
        "label": "Surface-Back-Standard",
        "cam": "back",
        "sensor": "ov8865",
        "cap_w": 1632,
        "cap_h": 1224,
        "out_w": 1632,
        "out_h": 1224,
        "fps": 30,
        "crop_mode": "native",
        "crop_r": 16,
        "crop_b": 8,
        "crop_l": 8,
        "prio": 20,
    },
    "back-hq": {
        "label": "Surface-Back-HQ",
        "cam": "back",
        "sensor": "ov8865",
        "cap_w": 3264,
        "cap_h": 2448,
        # Full-sensor capture, 1920×1440 publish — 8MP YUYV alone is ~15 fps CPU;
        # this still beats Standard 1632 and sustains ~15 unique fps.
        "out_w": 1920,
        "out_h": 1440,
        "fps": 15,
        "crop_mode": "native",
        "crop_r": 16,
        "crop_b": 8,
        "crop_l": 8,
        "prio": 30,
    },
    "back-fast": {
        "label": "Surface-Back-Fast",
        "cam": "back",
        "sensor": "ov8865",
        "cap_w": 800,
        "cap_h": 600,
        "out_w": 800,
        "out_h": 450,
        "fps": 60,
        "crop_mode": "crop",
        "crop_r": 8,
        "crop_b": 4,
        "crop_l": 4,
        "prio": 10,
    },
    "ir": {
        "label": "Surface-IR-Howdy",
        "cam": "ir",
        "sensor": "ov7251",
        "cap_w": 640,
        "cap_h": 480,
        "out_w": 480,
        "out_h": 640,
        "fps": 30,
        "crop_mode": "native",
        "crop_r": 0,
        "crop_b": 0,
        "crop_l": 0,
        "prio": 100,
    },
}
PROFILE_ORDER = [
    "front-standard",
    "front-hq",
    "front-fast",
    "back-standard",
    "back-hq",
    "back-fast",
    "ir",
]
LABEL_TO_PROFILE = {p["label"]: k for k, p in PROFILES.items()}

STREAM_LINE_OFF = 4
HINT = Path("/run/surface-webcam/active")
STATUS = Path("/run/surface-webcam/status")
IR_OWNER = Path("/run/surface-webcam/ir_owner")  # preview | howdy | idle
IR_LED_MODE = Path("/run/surface-webcam/ir_led_mode")  # steady | blink | off
BACK_USED = Path("/run/surface-ipu/back-used")
SWITCH_HOLD = 0.15
SWITCH_DWELL = 0.20
SWITCH_WAIT = 3.0
IDLE_SWITCH = 0.15
FAIL_BACKOFF = 8.0
DEAD_BACKOFF = 120.0
FRONT_LOCK_HOLD = 1.0
LINGER_S = 0.35
ISYS_DWELL = 0.30  # min suspended time before next STREAMON (was 2.0s)
ISYS_RUNTIME = Path(
    "/sys/devices/pci0000:00/0000:00:05.0/intel-ipu4-mmu0/intel-ipu60/power/runtime_status"
)
CACHE_DIR = Path("/var/cache/surface-webcam")
SKIP_COMMS = ("v4l2-ctl",)
LED = {
    "front": Path("/sys/class/leds/INT33BE_00::privacy_led/brightness"),
    "back": Path("/sys/class/leds/INT347A_00::privacy_led/brightness"),
}


def family(profile: str) -> str:
    if profile == "idle" or profile not in PROFILES:
        return "idle"
    return str(PROFILES[profile]["cam"])


def profiles_of(cam: str) -> list[str]:
    return [k for k in PROFILE_ORDER if PROFILES[k]["cam"] == cam]


def resolve_loopbacks() -> dict[str, str]:
    """Resolve by card name — Front-Standard should be the lowest number (V4L2 default)."""
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
        # Legacy 3-device labels → map to Standard profiles
        if name == "Surface-Front":
            found.setdefault("front-standard", f"/dev/{ent.name}")
        elif name == "Surface-Back":
            found.setdefault("back-standard", f"/dev/{ent.name}")
        pk = LABEL_TO_PROFILE.get(name)
        if pk:
            found[pk] = f"/dev/{ent.name}"
    # Fallbacks for 60–66 layout from modprobe
    defaults = {
        "front-standard": "/dev/video60",
        "front-hq": "/dev/video61",
        "front-fast": "/dev/video62",
        "back-standard": "/dev/video63",
        "back-hq": "/dev/video64",
        "back-fast": "/dev/video65",
        "ir": "/dev/video66",
    }
    for k, v in defaults.items():
        found.setdefault(k, v)
    return found


DEV = resolve_loopbacks()
_PW_CACHE = {"t": 0.0, "running": set()}
_PW_MIN_INTERVAL = 2.0  # pw-dump is expensive; never more often than this



def set_privacy_led(name: str, on: bool) -> None:
    p = LED.get(name)
    if p is None:
        return
    try:
        p.write_text("1\n" if on else "0\n")
    except OSError:
        pass


def isys_runtime() -> str:
    try:
        return ISYS_RUNTIME.read_text().strip()
    except OSError:
        return "unknown"


def wait_isys_idle(timeout: float = 3.0, min_dwell: float = ISYS_DWELL) -> bool:
    """Wait until ISYS firmware suspends — that resets AFE on every port."""
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
        time.sleep(0.03)
    print(f"ISYS still {isys_runtime()} after {timeout:.0f}s", flush=True)
    return False


def wait_switch_cycle() -> bool:
    """Brief settle after STREAMOFF — long dwells left the previous privacy LED on."""
    print(f"switch-settle {SWITCH_DWELL:.2f}s (isys={isys_runtime()})", flush=True)
    time.sleep(SWITCH_DWELL)
    return wait_isys_idle(timeout=max(SWITCH_WAIT, 2.0), min_dwell=ISYS_DWELL)


class LockedTone:
    def __init__(self, cam_tag: str = "") -> None:
        self.cam_tag = cam_tag
        self.lo: float | None = None
        self.hi: float | None = None
        self.scales: np.ndarray | None = None
        self._acc: list[tuple[np.ndarray, np.ndarray]] = []
        # HQ/live: lock WB on first good frame — multi-frame stack delayed first picture.
        self._need = 1

    def learn(self, img16: np.ndarray, bgr: np.ndarray) -> None:
        if self.scales is not None:
            p50 = float(np.percentile(img16[::8, ::8] if img16.size > 1_000_000 else img16, 50.0))
            mid = (self.lo + self.hi) * 0.5
            if mid > 1.0 and (p50 > mid * 2.4 or p50 < mid * 0.4):
                self.lo = self.hi = None
                self.scales = None
                self._acc.clear()
                print(f"WB relock p50={p50:.0f} mid={mid:.0f}", flush=True)
            else:
                return
        # HQ: learn on 1/4 sample — full 8MP float stacks crush FPS
        if img16.shape[0] >= 1500:
            img16 = img16[::4, ::4]
            bgr = bgr[::4, ::4]
        self._acc.append((img16.astype(np.float32), bgr.astype(np.float32)))
        if len(self._acc) < self._need:
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
        # ov8865: grey-world often leaves a light magenta on whites.
        # Soft pull of R/B toward G — avoid green overshoot.
        if self.cam_tag == "back":
            self.scales = self.scales.copy()
            g = float(self.scales[1])
            # Mild blend (25%) — strong blend caused cyan/green walls
            self.scales[0] = float(self.scales[0]) * 0.75 + g * 0.25  # B
            self.scales[2] = float(self.scales[2]) * 0.70 + g * 0.30  # R
            self.scales = np.clip(self.scales, 0.55, 1.85)
        self._acc.clear()
        print(
            f"WB locked raw[{self.lo:.0f}..{self.hi:.0f}] scales={self.scales}",
            flush=True,
        )

    def stretch16(self, img16: np.ndarray) -> np.ndarray:
        return self.stretch_any(img16)

    def stretch_any(self, img: np.ndarray) -> np.ndarray:
        x = img
        large = img.size > 2_000_000
        if self.lo is None or self.hi is None:
            sample = x[::8, ::8] if large else x
            p1, p99 = np.percentile(sample, (1.0, 99.0))
            if p99 >= 980 and p1 >= 200:
                return cv2.convertScaleAbs(x, alpha=200.0 / 1023.0)
            lo, hi = float(p1), float(max(p99, p1 + 16.0))
        else:
            lo, hi = self.lo, self.hi
        mid = max(hi - lo, 16.0)
        if large:
            # Linear stretch — Reinhard float32 on 8MP is ~5 fps.
            return cv2.convertScaleAbs(x, alpha=255.0 / mid, beta=-lo * 255.0 / mid)
        x = x.astype(np.float32)
        z = np.maximum(x - lo, 0.0)
        # Reinhard: hi → ~165, 1023 → ~240 — face kept, OLED highlights not crushed to 255
        b = mid * 0.55
        a = 165.0 * (mid + b) / mid
        y = a * z / (z + b)
        return np.clip(y, 0, 255).astype(np.uint8)

    def apply_wb_linear(self, bgr16: np.ndarray) -> np.ndarray:
        if self.scales is None:
            return bgr16
        if bgr16.size > 2_000_000:
            # Fixed-point Q8 per channel — skip float32 8MP alloc in the hot path
            out = np.empty_like(bgr16)
            for c in range(3):
                q = max(1, int(round(float(self.scales[c]) * 256.0)))
                ch = bgr16[:, :, c].astype(np.uint32)
                out[:, :, c] = np.minimum((ch * q) >> 8, 1023).astype(bgr16.dtype)
            return out
        return np.clip(bgr16.astype(np.float32) * self.scales, 0, 1023)


def detect_line_off(raw: bytes) -> int:
    """IPU: first frames have a 4 B CSI-2 header per line; later ones often do not."""
    if len(raw) < 4:
        return STREAM_LINE_OFF
    di, wcl, wch, ecc = raw[0], raw[1], raw[2], raw[3]
    # 0x2B/0x30/0x50 classic; 0x40 = VC1|DT0 seen on SB3 ov7251 face frames
    if di in (0x2B, 0x30, 0x50, 0x40) and wcl | wch:
        return 4
    return STREAM_LINE_OFF


def raw_good_line_frac(raw: bytes, h: int, stride: int, line_off: int) -> float:
    rows = min(h, max(1, len(raw) // max(stride, 1)))
    # Sample ≤64 rows — full scan was pure-Python overhead on every frame.
    step = max(1, rows // 64)
    off = line_off if line_off + 1 < stride else 0
    good = 0
    n = 0
    for y in range(0, rows, step):
        n += 1
        if raw[y * stride + off] != 0xFF:
            good += 1
    return good / float(max(n, 1))


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
    """Decode to BGR/grey after sensor edge crops — caller applies fit_frame."""
    need = stride * h
    if len(raw) < need:
        raw = raw + bytes(need - len(raw))
    line_off = detect_line_off(raw)
    if cam.ir:
        line_off = 4
    if raw_good_line_frac(raw, h, stride, line_off) < 0.40:
        return None
    if cam.ir:
        grey = sc.finish_ir(
            sc.unpack_mipi10_ir(raw[:need], w, h, stride, line_off=line_off), cam
        )
        return grey
    img16 = sc.unpack_mipi10_u16(raw[:need], w, h, stride, line_off=line_off)
    # Live path: bilinear demosaic (EA is too heavy for CFR target fps).
    try:
        from pycompat import bayer_code, resize_area

        bayer = bayer_code(cv2, prefer_fast=True)
    except Exception:
        bayer = cv2.COLOR_BayerRG2BGR
        resize_area = lambda _cv, im, wh: cv2.resize(im, wh, interpolation=cv2.INTER_AREA)
    try:
        bgr16 = cv2.cvtColor(img16, bayer)
    except cv2.error:
        bgr16 = cv2.cvtColor(img16, cv2.COLOR_BayerRG2BGR)
    bgr16 = sc._orient(bgr16, cam)
    # Downscale large frames before WB/stretch (HQ and Front-HQ).
    if w >= 2000:
        tw, th = (1920, 1440)
        bgr16 = resize_area(cv2, bgr16, (tw, th))
        img16 = resize_area(cv2, img16, (tw, th))
    if tone.scales is None:
        tone.learn(img16, tone.stretch_any(bgr16))
    bgr16 = tone.apply_wb_linear(bgr16)
    bgr = tone.stretch_any(bgr16)
    if tone.scales is None:
        sample = bgr[::8, ::8].reshape(-1, 3).astype(np.float32)
        lum = sample.mean(axis=1)
        mid = sample[(lum > 25) & (lum < 220)]
        if len(mid) < 40:
            mid = sample
        means = np.maximum(mid.mean(axis=0), 1.0)
        preview = np.clip(float(means.mean()) / means, 0.55, 1.85)
        if cam.tag == "back":
            g = float(preview[1])
            preview = preview.copy()
            preview[0] = float(preview[0]) * 0.75 + g * 0.25
            preview[2] = float(preview[2]) * 0.70 + g * 0.30
        bgr = np.clip(bgr.astype(np.float32) * preview, 0, 255).astype(np.uint8)
    hh, ww = bgr.shape[:2]
    r = min(crop_r, max(ww - 8, 0))
    btm = min(crop_b, max(hh - 8, 0))
    left = min(crop_l, max(ww - 8, 0))
    if r or btm or left:
        bgr = bgr[0 : hh - btm, left : ww - r]
    # Live: skip heavy front chroma blur (was killing unique_fps).
    return bgr


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


def _open_loopback(dev: str, w: int, h: int, fps: int = 30) -> int:
    last_err: OSError | None = None
    # If readers already hold exclusive_caps, never reset format (guvcview EBUSY/crash).
    try:
        if readers_of(dev):
            fd = os.open(dev, os.O_WRONLY | os.O_NONBLOCK)
            try:
                flg = fcntl.fcntl(fd, fcntl.F_GETFL)
                fcntl.fcntl(fd, fcntl.F_SETFL, flg | os.O_NONBLOCK)
            except OSError:
                pass
            if _fd_has_capture(fd):
                return fd
            os.close(fd)
    except OSError:
        pass
    for attempt in range(4):
        # Avoid keep_format=0 thrash on retry when format already matches.
        need_fmt = True
        try:
            cur = subprocess.run(
                ["v4l2-ctl", "-d", dev, "--get-fmt-video"],
                capture_output=True,
                text=True,
                timeout=2,
            ).stdout
            if f"Width/Height      : {w}/{h}" in cur and "YUYV" in cur:
                need_fmt = False
        except Exception:
            need_fmt = True
        if need_fmt:
            subprocess.run(["v4l2-ctl", "-d", dev, "-c", "keep_format=0"], capture_output=True)
            subprocess.run(
                [
                    "v4l2-ctl",
                    "-d",
                    dev,
                    "--set-fmt-video-out",
                    f"width={w},height={h},pixelformat=YUYV",
                    "--set-fmt-video",
                    f"width={w},height={h},pixelformat=YUYV",
                    "--set-parm",
                    str(fps),
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


def out_size(profile: str) -> tuple[int, int]:
    p = PROFILES.get(profile) or PROFILES["front-standard"]
    return int(p["out_w"]), int(p["out_h"])


def loading_yuyv(profile: str = "front-standard") -> bytes:
    """Dark frame with a caption — not a frozen sensor still. exclusive_caps stays Capture."""
    ow, oh = out_size(profile)
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
    cv2.putText(
        bgr, text, (x, y), cv2.FONT_HERSHEY_DUPLEX, scale, (210, 210, 210), thick, cv2.LINE_8
    )
    frame = sc.bgr_or_grey_to_yuyv(bgr)
    _LOADING[key] = frame
    return frame


def load_hold_frames() -> dict[str, bytes]:
    try:
        for p in CACHE_DIR.glob("*.yuyv"):
            p.unlink()
    except OSError:
        pass
    return {n: loading_yuyv(n) for n in PROFILE_ORDER}


def save_hold_frame(name: str, yuyv: bytes) -> None:
    return


def pw_running_cams() -> set[str]:
    now = time.time()
    if now - _PW_CACHE["t"] < _PW_MIN_INTERVAL:
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


_READERS_CACHE: dict[str, object] = {"t": 0.0, "map": {}}


def readers_of(dev: str) -> set[int]:
    """Anyone holding the loopback except us / v4l2-ctl — including pipewire on getUserMedia."""
    # Cache ~200ms — full /proc fd scans every CFR tick were starving decode (~1 fps).
    now = time.monotonic()
    cache_map: dict = _READERS_CACHE["map"]  # type: ignore[assignment]
    if now - float(_READERS_CACHE["t"]) < 0.20 and isinstance(cache_map, dict):
        hit = cache_map.get(dev)
        if hit is not None:
            return set(hit)
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
    if not isinstance(cache_map, dict) or now - float(_READERS_CACHE["t"]) >= 0.20:
        cache_map = {}
        _READERS_CACHE["map"] = cache_map
        _READERS_CACHE["t"] = now
    cache_map[dev] = set(out)
    return out


def refresh_readers_map() -> dict[str, set[int]]:
    """One /proc walk for all loopback devices (CFR hot path)."""
    global DEV
    now = time.monotonic()
    me = os.getpid()
    wanted = {dev: set() for dev in DEV.values() if dev}
    real_of = {}
    for dev in wanted:
        try:
            real_of[dev] = os.path.realpath(dev)
        except OSError:
            real_of[dev] = dev
    bases = {dev: dev.rsplit("/", 1)[-1] for dev in wanted}
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
        fd_dir = pid / "fd"
        try:
            names = os.listdir(fd_dir)
        except OSError:
            continue
        for name in names:
            try:
                t = os.readlink(fd_dir / name)
            except OSError:
                continue
            for dev in wanted:
                if t == dev or t.endswith(bases[dev]) or t == real_of[dev]:
                    wanted[dev].add(p)
                    break
    _READERS_CACHE["t"] = now
    _READERS_CACHE["map"] = dict(wanted)
    return wanted


def _cmd_looks_like_howdy(cmd: str) -> bool:
    """Match real Howdy / enroll / PAM — not harness scripts named *test-howdy-*."""
    c = cmd.lower()
    if "test-howdy-switch" in c or "test-ir-switch" in c:
        return False
    needles = (
        "pam_howdy",
        "howdy-gtk",
        "/usr/bin/howdy",
        "/usr/lib64/howdy",
        "/usr/lib/howdy",
        "surface-howdy-add-profile",
        "howdy --user",
        "howdy -u",
        "howdy add",
        "howdy test",
        "howdy clear",
    )
    return any(n in c for n in needles)


def howdy_holding_ir() -> bool:
    ir_dev = DEV.get("ir", "")
    if not ir_dev:
        return False
    for pid in readers_of(ir_dev):
        try:
            cmd = (
                Path(f"/proc/{pid}/cmdline")
                .read_bytes()
                .replace(b"\0", b" ")
                .decode("utf-8", "ignore")
            )
        except OSError:
            continue
        if _cmd_looks_like_howdy(cmd):
            return True
    return False


def enforce_ir_mutex() -> str:
    """Howdy owns IR exclusively — drop other IR readers. Returns owner tag."""
    ir_dev = DEV.get("ir", "")
    if not ir_dev or not IR_ENABLED:
        try:
            IR_OWNER.write_text("idle\n")
        except OSError:
            pass
        return "idle"
    howdy_pids = set()
    other = set()
    for pid in readers_of(ir_dev):
        try:
            cmd = (
                Path(f"/proc/{pid}/cmdline")
                .read_bytes()
                .replace(b"\0", b" ")
                .decode("utf-8", "ignore")
            )
        except OSError:
            continue
        if _cmd_looks_like_howdy(cmd):
            howdy_pids.add(pid)
        else:
            other.add(pid)
    if howdy_pids:
        for pid in other:
            try:
                os.kill(pid, signal.SIGTERM)
                print(f"IR mutex: dropped preview pid={pid} (Howdy owns IR)", flush=True)
            except OSError:
                pass
        owner = "howdy"
    elif other or howdy_pids:
        owner = "preview"
    else:
        owner = "idle"
    try:
        IR_OWNER.write_text(owner + "\n")
    except OSError:
        pass
    return owner


def _best_open_profile(cam: str, open_set: set[str]) -> str | None:
    cands = [p for p in profiles_of(cam) if p in open_set]
    if not cands:
        return None
    return max(cands, key=lambda p: PROFILES[p]["prio"])


def pick_wanted(current: str, has_seed: bool) -> str:
    pw = pw_running_cams()
    open_set: set[str] = set()
    for pk, dev in DEV.items():
        if readers_of(dev) or pk in pw:
            open_set.add(pk)
    ir_open = "ir" in open_set
    front_p = _best_open_profile("front", open_set)
    back_p = _best_open_profile("back", open_set)
    hint = ""
    if HINT.exists():
        try:
            hint = HINT.read_text().strip()
        except OSError:
            hint = ""
        if hint in ("front", "back"):
            # family hint → best open of that family
            pass
        elif hint not in PROFILES:
            hint = ""
    if IR_ENABLED and ir_open:
        return "ir"
    if front_p and back_p:
        if hint in PROFILES and family(hint) in ("front", "back") and hint in open_set:
            return hint
        if hint == "front":
            return front_p
        if hint == "back":
            return back_p
        return front_p
    if front_p:
        return front_p
    if back_p:
        return back_p
    return "idle"


def write_status(active: str, extra: str) -> None:
    try:
        STATUS.parent.mkdir(parents=True, exist_ok=True)
        STATUS.write_text(f"active={active} {extra}\n")
    except OSError:
        pass


def can_derive(active: str, target: str) -> bool:
    """True if target profile can be produced from active capture (same/smaller mode)."""
    if family(active) != family(target):
        return False
    a, t = PROFILES[active], PROFILES[target]
    return (a["cap_w"], a["cap_h"]) == (t["cap_w"], t["cap_h"]) or (
        a["cap_w"] * a["cap_h"] >= t["cap_w"] * t["cap_h"]
        and a["cap_w"] >= t["cap_w"]
    )


class Capture:
    def __init__(self) -> None:
        self.stop = threading.Event()
        self.cap: subprocess.Popen | None = None
        self.name = "idle"
        self.last_yuyv: dict[str, bytes | None] = {k: None for k in PROFILE_ORDER}
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
        self._af_pos = 400
        self._af_best = 400
        self._af_phase = "idle"
        self._af_idx = 0
        self._af_positions: list[int] = []
        self._af_best_score = -1.0
        self._af_settle_until = 0.0
        self._af_last_t = 0.0
        self._af_micro_dir = 1
        self._af_micro_cand = 400
        self._af_micro_base = -1.0

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
        """Arm IR flood once after STREAMON — long strobe span (looks continuous)."""
        self._stop_ir_led()
        self._ir_led_stop.clear()
        sc.ir_led(True)
        try:
            IR_LED_MODE.write_text("steady\n")
        except OSError:
            pass

        def _run() -> None:
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

    def _publish(self, active: str, img: np.ndarray) -> None:
        """YUYV for active profile + siblings that currently have readers."""
        fam = family(active)
        # Prefer cached reader map — never walk /proc from the decode thread.
        cache_map = _READERS_CACHE.get("map") if isinstance(_READERS_CACHE.get("map"), dict) else {}
        for pk in profiles_of(fam):
            if not can_derive(active, pk):
                continue
            if pk != active:
                dev = DEV.get(pk, "")
                if not cache_map.get(dev):
                    continue
            p = PROFILES[pk]
            framed = sc.fit_frame(img, p["out_w"], p["out_h"], p["crop_mode"])
            if fam == "ir":
                ow, oh = p["out_w"], p["out_h"]
                if framed.shape[0] != oh or framed.shape[1] != ow:
                    continue
            self.last_yuyv[pk] = sc.bgr_or_grey_to_yuyv(framed)

    def _af_tick(self, bgr: np.ndarray) -> None:
        """Coarse sweep then continuous micro-hunt on DW9719 (Back only)."""
        now = time.monotonic()
        if now < self._af_settle_until:
            return
        hh, ww = bgr.shape[:2]
        roi = sc.af_roi(hh, ww)
        if self._af_phase == "coarse":
            if self._af_idx >= len(self._af_positions):
                sc.focus_open()
                sc.set_focus(self._af_best)
                self._af_pos = self._af_best
                self._af_phase = "track"
                self._af_last_t = now
                self._af_settle_until = now + 0.3
                print(f"AF coarse done best={self._af_best} score={self._af_best_score:.1f}", flush=True)
                return
            pos = self._af_positions[self._af_idx]
            if self._af_idx == 0 or pos != self._af_pos:
                sc.focus_open()
                sc.set_focus(pos)
                self._af_pos = pos
                self._af_settle_until = now + 0.22
                return
            score = sc.contrast_score(bgr, roi)
            if score > self._af_best_score:
                self._af_best_score = score
                self._af_best = pos
            self._af_idx += 1
            if self._af_idx < len(self._af_positions):
                nxt = self._af_positions[self._af_idx]
                sc.focus_open()
                sc.set_focus(nxt)
                self._af_pos = nxt
                self._af_settle_until = now + 0.22
            return
        if self._af_phase == "track" and now - self._af_last_t >= getattr(
            self, "_af_track_iv", 2.5
        ):
            self._af_last_t = now
            step = 32
            cand = int(np.clip(self._af_best + self._af_micro_dir * step, 0, 1023))
            sc.focus_open()
            sc.set_focus(cand)
            self._af_pos = cand
            self._af_settle_until = now + 0.2
            self._af_phase = "micro-score"
            self._af_micro_cand = cand
            self._af_micro_base = self._af_best_score
            return
        if self._af_phase == "micro-score":
            score = sc.contrast_score(bgr, roi)
            if score > self._af_best_score * 1.02:
                self._af_best = self._af_micro_cand
                self._af_best_score = score
            else:
                self._af_micro_dir *= -1
                sc.focus_open()
                sc.set_focus(self._af_best)
                self._af_pos = self._af_best
            self._af_phase = "track"
            self._af_settle_until = now + 0.15

    def start(self, name: str) -> None:
        if name not in PROFILES:
            raise RuntimeError(f"unknown profile {name}")
        cfg = PROFILES[name]
        cam_key = cfg["cam"]
        # Extinguish other privacy LEDs before any settle wait (front→back was ~10s).
        for led_name in LED:
            if led_name != cam_key:
                set_privacy_led(led_name, False)
        reap_orphan_holds(keep_pid=self.cap.pid if self.cap else None)
        ok, msg = ig.health_check()
        if not ok:
            raise RuntimeError(f"IPU unhealthy: {msg}")
        cam = sc.CAMS[cam_key]
        sensor = cfg["sensor"]
        for other in ("ov5693", "ov8865", "ov7251"):
            if other == sensor:
                continue
            sc._sensor_pm_write(other, "auto")
            sc.sensor_runtime_cycle(other, timeout=6.0)
        if cam_key == "ir":
            print(f"IR: skip switch-cycle (isys={isys_runtime()})", flush=True)
            self._front_sterile = False
        elif cam_key == "front" or self._from_back or self._need_cycle or self._front_sterile:
            print(f"switch-cycle before {name} (isys={isys_runtime()})", flush=True)
            wait_switch_cycle()
            self._front_sterile = False
        self._from_back = False
        self._need_cycle = False
        if cam_key == "front" and self._front_retries:
            print(f"Front retry {self._front_retries} after ISYS cycle", flush=True)
        sc._sensor_pm_write(sensor, "auto")
        if not sc.sensor_runtime_cycle(sensor, timeout=3.0):
            sc.run(["media-ctl", "-d", "/dev/media0", "-r"], check=False)
            sc._sensor_pm_write(sensor, "auto")
            if not sc.sensor_runtime_cycle(sensor, timeout=8.0):
                raise RuntimeError(
                    f"{sensor} not suspended ({sc.sensor_runtime_status(sensor)})"
                )
        if cam_key == "ir":
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

        if cam_key == "ir":
            Path("/sys/module/intel_ipu4p_isys/parameters/sb3_ir_clk_ticks").write_text("1155")
            Path("/sys/module/intel_ipu4p_isys/parameters/sb3_ir_data_ticks").write_text("1269")
        if cam_key in ("ir", "front"):
            try:
                fd = os.open(
                    "/dev/video5" if cam_key == "ir" else "/dev/video10",
                    os.O_RDWR | os.O_NONBLOCK,
                )
                os.close(fd)
            except OSError as e:
                Path("/run/surface-ipu/isys-dead").write_text(
                    f"{time.strftime('%Y-%m-%dT%H:%M:%S')} open fail {e}\n"
                )
                raise RuntimeError(f"ISYS dead before {name} STREAMON: {e}") from e
        dev = sc.media_setup(cam, cfg["cap_w"], cfg["cap_h"])
        sc.set_sensor_exposure(sensor, cam.exposure, cam.gain)
        if cam_key == "back":
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
                        f"vertical_blanking=24,red_balance={sc.OV8865_RED_BALANCE},"
                        f"blue_balance={sc.OV8865_BLUE_BALANCE}",
                    ],
                    capture_output=True,
                )
            else:
                sc.set_ov8865_wb()
            # AF is best-effort — never abort Back STREAMON if VCM open fails (EINVAL).
            # HQ: skip coarse sweep (blocks decode ~3s + I2C); mid focus + rare micro-hunt.
            is_hq = cfg["cap_w"] >= 2000
            self._af_positions = list(range(64, 1024, 64)) + [1023]
            self._af_idx = 0
            self._af_best = 400
            self._af_best_score = -1.0
            self._af_phase = "idle"
            self._af_settle_until = 0.0
            self._af_micro_dir = 1
            try:
                lens = sc.focus_open()
                if lens and sc.set_focus(self._af_best):
                    self._af_pos = self._af_best
                    if is_hq:
                        print("AF: HQ — mid focus, micro-hunt only", flush=True)
                        self._af_phase = "track"
                        self._af_last_t = time.monotonic()
                        self._af_track_iv = 4.0
                    else:
                        self._af_phase = "coarse"
                        self._af_track_iv = 2.5
                else:
                    print(
                        "AF: DW9719 unavailable — Back streams without focus hunt",
                        flush=True,
                    )
                    self._af_phase = "idle"
            except Exception as e:
                print(f"AF init skip (stream continues): {e}", flush=True)
                self._af_phase = "idle"
                try:
                    sc.focus_close()
                except Exception:
                    pass
        else:
            self._af_phase = "idle"
            try:
                sc.focus_close()
            except Exception:
                pass
        if cam_key == "ir":
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
        # Live path: NO one-shot auto_expose — each capture_raw STREAMON blinks the
        # privacy LED and delays start 3–4×. Continuous AE after raw_hold is enough.
        stride = sc.get_stride(dev, cfg["cap_w"])
        fl = stride * cfg["cap_h"]
        print(
            f"capture {name} {dev} {cfg['cap_w']}x{cfg['cap_h']} "
            f"out={cfg['out_w']}x{cfg['out_h']} stride={stride} fl={fl}",
            flush=True,
        )
        self.stop.clear()
        self.name = name
        # [seq, payload] — seq bumps every frame so decode never sticks on `is` identity.
        latest_raw: list = [0, None]
        self.stats = {"got": 0, "drop": 0, "dec": 0, "out": self.stats.get("out", 0)}
        # Main-loop unique_fps baseline must reset with the stream.
        self._dec_log_base = 0
        hold = ensure_raw_hold()
        fourcc = cam.fourcc if len(cam.fourcc) == 4 else "pBAA"
        v4l_cmd = [hold, dev, str(cfg["cap_w"]), str(cfg["cap_h"]), fourcc]
        err_path = Path("/run/surface-webcam") / f"raw_hold-{name}.err"
        try:
            err_f = err_path.open("wb")
        except OSError:
            err_f = subprocess.DEVNULL
        # SHM double-buffer — bypasses fs.pipe-max-size (often 1 MiB); RGB frames are larger.
        shm_prefix = f"/dev/shm/surface-raw-{name}"
        use_shm = True
        try:
            with open(hold, "rb") as _hf:
                use_shm = b"SURFACE_RAW_SHM" in _hf.read()
        except OSError:
            use_shm = False
        env = os.environ.copy()
        if use_shm:
            env["SURFACE_RAW_SHM"] = shm_prefix
        else:
            env.pop("SURFACE_RAW_SHM", None)
            print(f"WARN raw_hold {hold} has no SHM — pipe fallback (slow if >1MiB)", flush=True)
        self.cap = subprocess.Popen(
            v4l_cmd,
            stdout=subprocess.PIPE,
            stderr=err_f,
            env=env,
        )
        if err_f is not subprocess.DEVNULL:
            try:
                err_f.close()
            except OSError:
                pass
        assert self.cap.stdout
        # Header pipe is tiny (8 B/frame); bump buffer anyway. Pipe fallback needs max.
        try:
            fcntl.fcntl(self.cap.stdout.fileno(), fcntl.F_SETPIPE_SZ, 1 << 20)
        except OSError:
            pass
        os.set_blocking(self.cap.stdout.fileno(), False)
        tone = LockedTone(cam_tag=cam_key)
        set_privacy_led(cam_key, True)
        print(f"capture hold={hold} shm={use_shm} fl={fl}", flush=True)
        try:
            from pycompat import set_num_threads

            set_num_threads(cv2, 1)
        except Exception:
            try:
                cv2.setNumThreads(1)
            except Exception:
                pass

        def _reader() -> None:
            """SHM+ctl: poll /dev/shm/*.ctl (no pipe/GIL stall). Else: pipe frames."""
            buf = bytearray()
            pipe_need = fl
            ctl_path = f"{shm_prefix}.ctl"
            last_seq = -1
            while not self.stop.is_set() and self.cap and self.cap.poll() is None:
                if use_shm:
                    try:
                        with open(ctl_path, "rb") as cf:
                            hdr = cf.read(8)
                    except OSError:
                        hdr = b""
                    if len(hdr) == 8:
                        seq, length = struct.unpack("<II", hdr)
                        if seq != last_seq and 0 < length <= fl + 64:
                            last_seq = seq
                            slot = seq & 1
                            path = f"{shm_prefix}.{slot}"
                            try:
                                with open(path, "rb") as sf:
                                    raw = sf.read(length)
                            except OSError:
                                time.sleep(0.0005)
                                continue
                            if len(raw) < length:
                                time.sleep(0.0005)
                                continue
                            if len(raw) < fl:
                                raw = raw + bytes(fl - len(raw))
                            elif len(raw) > fl:
                                raw = raw[:fl]
                            latest_raw[0] = int(latest_raw[0]) + 1
                            latest_raw[1] = raw
                            self.stats["got"] += 1
                            got = self.stats["got"]
                            if got <= 1 or (got in (20, 40) and cfg["cap_w"] < 2000):
                                try:
                                    loff = detect_line_off(raw)
                                    frac = raw_good_line_frac(raw, cfg["cap_h"], stride, loff)
                                    print(
                                        f"RAW {name} n={got} len={len(raw)} off={loff} "
                                        f"good={frac:.2f} mode=shm-ctl",
                                        flush=True,
                                    )
                                except Exception as e:
                                    print(f"RAW dump fail {e}", flush=True)
                            continue
                    # Drain pipe so it never blocks raw_hold (compat headers).
                    if self.cap.stdout:
                        try:
                            while self.cap.stdout.read(256):
                                pass
                        except BlockingIOError:
                            pass
                        except Exception:
                            pass
                    time.sleep(0.0005)
                    continue
                # Pipe fallback (old raw_hold)
                try:
                    chunk = self.cap.stdout.read(65536) if self.cap.stdout else b""
                except BlockingIOError:
                    chunk = b""
                if not chunk:
                    if self.cap.poll() is not None:
                        print("raw_hold died", self.cap.returncode, flush=True)
                        self.stop.set()
                        return
                    time.sleep(0.0005)
                    continue
                buf.extend(chunk)
                if len(buf) < pipe_need:
                    continue
                nframe = len(buf) // pipe_need
                if nframe > 1:
                    self.stats["drop"] += nframe - 1
                    del buf[: (nframe - 1) * pipe_need]
                raw = bytes(buf[:pipe_need])
                del buf[:pipe_need]
                if len(raw) < fl:
                    raw = raw + bytes(fl - len(raw))
                elif len(raw) > fl:
                    raw = raw[:fl]
                latest_raw[0] = int(latest_raw[0]) + 1
                latest_raw[1] = raw
                self.stats["got"] += 1
                got = self.stats["got"]
                if got <= 1 or got in (20, 40):
                    print(f"RAW {name} n={got} len={len(raw)} mode=pipe", flush=True)

        def _decode() -> None:
            last_seq = 0
            ae_n = 0
            ae_exp = int(cam.exposure)
            ae_gain = int(cam.gain)
            ae_last_t = 0.0
            ir_mean_ema: float | None = None
            is_hq = cfg["cap_w"] >= 2000
            ae_interval = 2.5 if is_hq else 1.0
            stall_t = time.monotonic()
            while not self.stop.is_set():
                seq = int(latest_raw[0])
                raw = latest_raw[1]
                if raw is None or seq == last_seq:
                    if time.monotonic() - stall_t > 3.0 and self.stats["got"] > 0:
                        print(
                            f"decode stall got={self.stats['got']} dec={self.stats['dec']} "
                            f"seq={seq}",
                            flush=True,
                        )
                        stall_t = time.monotonic()
                    time.sleep(0.0005)
                    continue
                last_seq = seq
                stall_t = time.monotonic()
                try:
                    if cam_key == "ir":
                        sample = raw[::64]
                        if sample and (sum(sample) / len(sample)) > 240:
                            continue
                    bgr = decode(
                        cam,
                        raw,
                        cfg["cap_w"],
                        cfg["cap_h"],
                        stride,
                        tone,
                        cfg["crop_r"],
                        cfg["crop_b"],
                        cfg.get("crop_l", 0),
                    )
                    if bgr is None:
                        continue
                    if cam_key == "ir":
                        ow, oh = out_size(name)
                        expect = ow * oh * 2
                        mean = float(np.mean(bgr))
                        if ir_mean_ema is None:
                            ir_mean_ema = mean
                        else:
                            if abs(mean - ir_mean_ema) > max(28.0, ir_mean_ema * 0.45):
                                prev_y = self.last_yuyv.get(name)
                                if prev_y is not None and len(prev_y) == expect:
                                    ir_mean_ema = 0.85 * ir_mean_ema + 0.15 * mean
                                    continue
                            ir_mean_ema = 0.82 * ir_mean_ema + 0.18 * mean
                        prev_y = self.last_yuyv.get(name)
                        if mean < 8.0 and prev_y is not None and len(prev_y) == expect:
                            continue
                        if bgr.shape[0] != oh or bgr.shape[1] != ow:
                            continue
                    ae_n += 1
                    now = time.monotonic()
                    if cam_key == "ir" and ae_n >= 60 and now - ae_last_t >= 3.5:
                        ae_last_t = now
                        try:
                            img16 = sc.unpack_mipi10_u16(
                                raw, cfg["cap_w"], cfg["cap_h"], stride, line_off=4
                            )
                            hh, ww = img16.shape
                            crop = img16[hh // 5 : (4 * hh) // 5, ww // 5 : (4 * ww) // 5]
                            p95 = float(np.percentile(crop, 95))
                            sat = float((crop > 900).mean())
                            new_exp, new_gain = ae_exp, ae_gain
                            if p95 >= 80:
                                if sat > 0.02 or p95 > 720:
                                    new_exp = max(35, ae_exp - 4)
                                    new_gain = max(8, ae_gain - 1) if ae_exp <= 42 else ae_gain
                                elif p95 < 180:
                                    new_exp = min(160, ae_exp + 3)
                                    new_gain = ae_gain
                            if new_exp != ae_exp or new_gain != ae_gain:
                                sc.set_sensor_exposure("ov7251", new_exp, new_gain)
                                ae_exp, ae_gain = new_exp, new_gain
                                cam.exposure, cam.gain = new_exp, new_gain
                                print(
                                    f"IR AE slow exp={ae_exp} gain={ae_gain} "
                                    f"p95={p95:.0f} sat={sat:.3f}",
                                    flush=True,
                                )
                        except Exception as e:
                            print(f"IR AE skip: {e}", flush=True)
                    elif (
                        cam_key in ("front", "back")
                        and ae_n >= 30
                        and now - ae_last_t >= ae_interval
                    ):
                        ae_last_t = now
                        try:
                            hh, ww = bgr.shape[:2]
                            crop = bgr[hh // 4 : (3 * hh) // 4, ww // 4 : (3 * ww) // 4]
                            g = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
                            mean = float(g.mean())
                            sat = float((g > 245).mean())
                            new_exp, new_gain = sc.continuous_ae_rgb(
                                sensor,
                                mean,
                                sat,
                                ae_exp,
                                ae_gain,
                                target=float(cam.target_mean),
                            )
                            if new_exp != ae_exp or new_gain != ae_gain:
                                sc.set_sensor_exposure(sensor, new_exp, new_gain)
                                ae_exp, ae_gain = new_exp, new_gain
                                cam.exposure, cam.gain = new_exp, new_gain
                                print(
                                    f"{cam_key} AE cont exp={ae_exp} gain={ae_gain} "
                                    f"mean={mean:.0f} sat={sat:.3f}",
                                    flush=True,
                                )
                        except Exception as e:
                            print(f"{cam_key} AE skip: {e}", flush=True)
                    if cam_key == "back" and self._af_phase != "idle":
                        # AF I2C must not run in the decode hot path — it stalls CFR
                        # to ~0 unique frames on Back. Tick at most ~2 Hz.
                        if not hasattr(self, "_af_last_decode_t"):
                            self._af_last_decode_t = 0.0
                        if now - self._af_last_decode_t >= 0.5:
                            self._af_last_decode_t = now
                            try:
                                self._af_tick(bgr)
                            except Exception as e:
                                print(f"AF skip: {e}", flush=True)
                    self._publish(name, bgr)
                    self.stats["dec"] += 1
                except Exception as e:
                    print(f"decode err {name}: {e}", flush=True)
                    time.sleep(0.01)

        self._threads = [
            threading.Thread(target=_reader, daemon=True, name=f"cap-{name}"),
            threading.Thread(target=_decode, daemon=True, name=f"dec-{name}"),
        ]
        for t in self._threads:
            t.start()
        if cam_key == "ir":
            self._start_ir_led()

            def _rear_led() -> None:
                time.sleep(0.6)
                if not self.stop.is_set() and family(self.name) == "ir":
                    sc.ir_led(True)
                    print("IR LED re-armed (span≈exposure)", flush=True)

            threading.Thread(target=_rear_led, daemon=True, name="ir-led-rear").start()

    def halt(self, force: bool = False) -> bool:
        """Clean STREAMOFF (TERM, never SIGKILL). False = do not start the next STREAMON."""
        prev = self.name
        prev_fam = family(prev)
        # Kill privacy LED immediately — must not wait for sensor suspend (~seconds).
        if prev_fam in LED:
            set_privacy_led(prev_fam, False)
        if prev not in PROFILES and self.cap is None:
            return True
        already_off = self.cap is not None and self.cap.poll() is not None
        zero_stream = (
            prev_fam in ("front", "ir")
            and self.stats.get("got", 0) < 5
            and self.cap is not None
            and not already_off
        )
        ir_unlocked_ff = (
            prev_fam == "ir"
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
        if already_off and prev_fam == "front" and self.stats.get("dec", 0) == 0:
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
                cap.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                print("STREAMOFF timeout — leave IPU alone", flush=True)
                set_privacy_led(prev_fam, False)
                return False
        for t in self._threads:
            t.join(timeout=1.0)
        alive = [t.name for t in self._threads if t.is_alive()]
        if alive:
            print(f"WARN leftover threads {alive} — replacing stop Event", flush=True)
            self.stop = threading.Event()
            self.stop.set()
        self._threads = []
        if prev not in PROFILES:
            ok, msg = ig.health_check()
            return ok
        sensor = PROFILES[prev]["sensor"]
        sc._sensor_pm_write(sensor, "auto")
        sc.sensor_runtime_cycle(sensor, timeout=2.5)
        set_privacy_led(prev_fam, False)
        if prev_fam == "ir":
            self._stop_ir_led()
            self._need_cycle = True
        elif prev_fam == "back":
            self._from_back = True
            self._need_cycle = True
            self._af_phase = "idle"
            sc.focus_close()
            time.sleep(0.1)
        elif prev_fam == "front":
            self._need_cycle = True
        for pk in profiles_of(prev_fam):
            self.last_yuyv[pk] = loading_yuyv(pk)
        if skip_health:
            return True
        ok, msg = ig.health_check()
        if not ok:
            print(f"health after STREAMOFF: {msg}", flush=True)
            return False
        return True


def reap_orphan_holds(keep_pid: int | None = None) -> None:
    """KillMode=process can leave raw_hold on video10 — ov5693 then never suspends."""
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
    print(
        "loopbacks "
        + " ".join(f"{k}={DEV[k]}" for k in PROFILE_ORDER),
        flush=True,
    )
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
    for name in PROFILE_ORDER:
        ow, oh = out_size(name)
        fps = int(PROFILES[name]["fps"])
        fds[name] = _open_loopback(DEV[name], ow, oh, fps)
        print(f"loopback {name} {DEV[name]} {ow}x{oh} @{fps}", flush=True)

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
    cfr_fps = 30

    def _reopen(name: str) -> None:
        # Reader may still hold exclusive_caps — WRONLY open then fails EBUSY.
        # Never storm reopen while apps are attached.
        try:
            if readers_of(DEV.get(name, "")):
                return
        except Exception:
            pass
        old = fds.get(name, -1)
        if old >= 0:
            try:
                os.close(old)
            except OSError:
                pass
        fds[name] = -1
        DEV.update(resolve_loopbacks())
        dev = DEV.get(name, "")
        # guvcview/crash can race; never open a non-char path (Errno 21 Is a directory)
        try:
            st = os.stat(dev)
            if not stat.S_ISCHR(st.st_mode):
                print(f"loopback reopen {name} skip non-char {dev}", flush=True)
                return
        except OSError as e:
            print(f"loopback reopen {name} missing {dev}: {e}", flush=True)
            return
        try:
            ow, oh = out_size(name)
            fps = int(PROFILES[name]["fps"])
            fds[name] = _open_loopback(dev, ow, oh, fps)
            print(f"loopback writer {name} {dev} {ow}x{oh}", flush=True)
        except OSError as e:
            fds[name] = -1
            if getattr(e, "errno", None) in (errno.EBUSY, errno.EAGAIN):
                return
            print(f"loopback reopen {name} {e}", flush=True)

    def _cfr() -> None:
        last_full = {n: time.monotonic() for n in PROFILE_ORDER}
        while not stop.is_set():
            t0 = time.time()
            period = 1.0 / max(15, cfr_fps)
            # Match active profile FPS when streaming
            active = cap.name
            if active in PROFILES:
                period = 1.0 / max(1, int(PROFILES[active]["fps"]))
            tick["n"] += 1
            # /proc scan at most ~4 Hz — was every CFR tick and starved decode.
            if tick["n"] == 1 or tick["n"] % max(1, int(0.25 / max(period, 0.01))) == 0:
                rmap = refresh_readers_map()
            else:
                rmap = _READERS_CACHE.get("map") or {}
                if not isinstance(rmap, dict):
                    rmap = refresh_readers_map()
            for name in PROFILE_ORDER:
                fd = fds.get(name, -1)
                dev = DEV.get(name, "")
                has_rd = bool(rmap.get(dev))
                if fd < 0:
                    if not has_rd:
                        _reopen(name)
                    continue
                is_active = name == active
                # Only push frames to active profile + current readers.
                if not is_active and not has_rd:
                    if tick["n"] % 30 != 0:
                        continue
                if tick["n"] % 45 == 0 and not _fd_has_capture(fd):
                    if has_rd:
                        continue
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
                elif n < 0:
                    if not has_rd:
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
        "7 profiles, Front-Standard default)",
        flush=True,
    )
    while not stop.is_set():
        now = time.monotonic()
        if now - t_log >= 2.0:
            dec = cap.stats["dec"]
            got = cap.stats.get("got", 0)
            inst = (dec - n_log) / max(now - t_log, 1e-6)
            # After start() resets dec to 0, re-baseline so unique_fps isn't negative.
            if dec < n_log:
                n_log = dec
                inst = 0.0
            owner = enforce_ir_mutex()
            led_mode = "off"
            try:
                led_mode = IR_LED_MODE.read_text().strip() or "off"
            except OSError:
                pass
            rf = {p: sorted(readers_of(DEV[p])) for p in profiles_of("front")}
            rb = {p: sorted(readers_of(DEV[p])) for p in profiles_of("back")}
            extra = (
                f"n={dec} got={got} unique_fps={inst:.1f} drop={cap.stats['drop']} "
                f"out={cap.stats['out']} cam={cap.name} "
                f"ir_owner={owner} ir_led={led_mode} "
                f"rf={rf} rb={rb} "
                f"ri={sorted(readers_of(DEV['ir']))} "
                f"pw={sorted(pw_running_cams())}"
            )
            print(f"LIVE {extra}", flush=True)
            write_status(cap.name, extra)
            t_log = now
            n_log = dec
        has_seed = any(cap.last_yuyv.get(n) for n in PROFILE_ORDER)
        pw = pw_running_cams()
        if any(readers_of(DEV[p]) or p in pw for p in profiles_of("front")):
            last_front_rd = now
        if any(readers_of(DEV[p]) or p in pw for p in profiles_of("back")):
            last_back_rd = now
        if readers_of(DEV["ir"]) or "ir" in pw:
            last_ir_rd = now
        if IR_ENABLED and (readers_of(DEV["ir"]) or "ir" in pw or family(cap.name) == "ir"):
            enforce_ir_mutex()
        if cap._hold_only:
            time.sleep(0.25)
            continue
        hold = IDLE_SWITCH if cap.name == "idle" else SWITCH_HOLD
        if family(cap.name) == "front" and cap.stats["dec"] == 0:
            hold = FRONT_LOCK_HOLD
        if now - last_switch >= hold:
            want = pick_wanted(cap.name, has_seed)
            if now - last_ir_rd < LINGER_S and IR_ENABLED and cap.stats.get("dec", 0) > 0:
                want = "ir"
            elif (
                now - last_front_rd < LINGER_S
                and not cap._front_sterile
                and cap.stats.get("dec", 0) > 0
                and want == "idle"
                and family(cap.name) == "front"
            ):
                # Brief fd drop — keep same Front profile. Do NOT pin Standard when
                # pick_wanted already chose Front-HQ/Fast (profile switch within family).
                want = cap.name
            elif (
                now - last_back_rd < LINGER_S
                and cap.stats.get("dec", 0) > 0
                and want == "idle"
                and family(cap.name) == "back"
            ):
                want = cap.name
            if (
                family(want) == "front"
                and family(cap.name) == "front"
                and want == cap.name
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
            if family(cap.name) == "ir" and cap.stats.get("dec", 0) == 0:
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
                        if family(cap.name) == "ir" and cap.stats.get("dec", 0) == 0:
                            cap._hold_only = True
                            cap._backoff_until = time.monotonic() + 3600
                            print("IR hold-only (dec=0) — no idle storm", flush=True)
                        last_switch = time.monotonic()
                        continue
                    cap.name = "idle"
                set_privacy_led("front", False)
                set_privacy_led("back", False)
                cap._stop_ir_led()
                # Do NOT reopen all loopbacks on idle — resets exclusive_caps under
                # guvcview/browser and causes EBUSY / SIGSEGV.
                last_switch = time.monotonic()
            else:
                if now < cap._backoff_until:
                    continue
                # Same sensor, different mode — need STREAMOFF + new capture
                print(
                    f"switch {cap.name} → {want}",
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
                    # Leave media graph clean so the next profile can STREAMON.
                    try:
                        sc.run(["media-ctl", "-d", "/dev/media0", "-r"], check=False)
                    except Exception:
                        pass
                    try:
                        sc.focus_close()
                    except Exception:
                        pass
                    cap.name = "idle"
                    err = str(e).lower()
                    if "unhealthy" in err or "dead" in err or "hang" in err:
                        cap._hold_only = True
                        cap._backoff_until = time.monotonic() + DEAD_BACKOFF
                        print(
                            f"ISYS dead — hold-only {DEAD_BACKOFF:.0f}s, no storm",
                            flush=True,
                        )
                    else:
                        # Soft failure (AF/format/etc.) — retry soon, not 8s black hole
                        cap._backoff_until = time.monotonic() + 1.5
                    last_switch = time.monotonic()
        time.sleep(0.25)

    cap.halt(force=True)
    set_privacy_led("front", False)
    set_privacy_led("back", False)
    cap._stop_ir_led()
    sc.focus_close()
    for fd in fds.values():
        try:
            os.close(fd)
        except OSError:
            pass


if __name__ == "__main__":
    main()
