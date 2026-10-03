#!/usr/bin/env python3
"""Always-on Surface Book 3 webcams — live frames, no placeholders, no terminal.

IPU4 = one sensor at a time. Front-Standard is the default camera (video60).
Resolve loopbacks by card name (7 devices):
  Surface-Front-{Standard,HQ,Fast}, Surface-Back-{Standard,HQ,Fast}, Surface-IR-Howdy
(/dev/video* numbers may be 60–66 or swapped).
One physical STREAMON at a time; opening any profile preempts the rest.
IR LED: continuous ON while IR streams. Status tracks howdy vs preview.

SURFACE_WEBCAM_IR=1 enables IR (required for Howdy). Set 0 to keep IR off.
"""
from __future__ import annotations

import fcntl
import ctypes
import errno
import json
import mmap
import os

# Cap BLAS/OpenCV pools before numpy/cv2 load — otherwise idle sits at ~15–25% CPU.
os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
os.environ.setdefault("NUMEXPR_NUM_THREADS", "1")
os.environ.setdefault("VECLIB_MAXIMUM_THREADS", "1")

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
_OPT_ROOT = "/opt/surface-cameras"
# Optional overlay (packaging / local checkout). Never hard-code a home path.
_EXTRA_ROOT = os.environ.get("SURFACE_CAMERAS_ROOT", "").strip()


def _vendor_paths_for(py: tuple[int, int] | None = None) -> list[str]:
    """ABI-matched OpenCV/numpy dirs. Never mix vendor/ (cp314) with vendor-py315."""
    maj, min_ = py or sys.version_info[:2]
    tag = "vendor-py315" if (maj, min_) >= (3, 15) else "vendor"
    out: list[str] = []
    roots = [_BOOT_ROOT, _OPT_ROOT]
    if _EXTRA_ROOT:
        roots.insert(0, _EXTRA_ROOT)
    for root in roots:
        out.append(os.path.join(root, tag))
        out.append(os.path.join(root, "lib"))
    out.append(os.path.expanduser(f"~/.local/lib/python{maj}.{min_}/site-packages"))
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


def _assert_vendor_deps() -> None:
    """Refuse mixed ABI: numpy/cv2 must both live under the same vendor* tree."""
    try:
        import numpy as _np
        import cv2 as _cv
    except Exception as e:
        raise SystemExit(f"surface_webcamd: numpy/cv2 import failed: {e}") from e
    np_f = getattr(_np, "__file__", "") or ""
    cv_f = getattr(_cv, "__file__", "") or ""
    maj, min_ = sys.version_info[:2]
    want = "vendor-py315" if (maj, min_) >= (3, 15) else "/vendor/"
    # Accept site-packages only when vendor trees are absent.
    vendors = _vendor_paths_for((maj, min_))
    vendor_roots = [p for p in vendors if "vendor" in p]
    if vendor_roots:
        def _ok(path: str) -> bool:
            n = path.replace("\\", "/")
            if (maj, min_) >= (3, 15):
                return "vendor-py315" in n
            return "/vendor/" in n and "vendor-py315" not in n

        if not _ok(np_f) or not _ok(cv_f):
            raise SystemExit(
                "surface_webcamd: ABI mismatch — "
                f"numpy={np_f!r} cv2={cv_f!r} want={want!r}. "
                "Fix PYTHONPATH / reinstall vendor wheels."
            )
    print(
        f"deps numpy={getattr(_np, '__version__', '?')}@{np_f} "
        f"cv2={getattr(_cv, '__version__', '?')}@{cv_f}",
        flush=True,
    )


# CPython 3.14+/3.15+ + OpenCV5 feature gates (eager fallback on older).
try:
    from pycompat import CV2_V5, MODERN_PY, PY315_PLUS  # noqa: F401
except ImportError:  # pragma: no cover
    CV2_V5 = False
    MODERN_PY = sys.version_info >= (3, 14)
    PY315_PLUS = sys.version_info >= (3, 15)

_assert_vendor_deps()
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
_TREE_HOLD = os.path.join(_BOOT_ROOT, "tools", "raw_hold")


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
# Front-Standard is default (lowest video_nr).
PROFILES: dict[str, dict] = {
    "front-standard": {
        "label": "Surface-Front-Standard",
        "cam": "front",
        "sensor": "ov5693",
        "cap_w": 1296,
        "cap_h": 972,
        # Discord Electron rejects 4:3 (1280x960) → green static / UI freeze.
        "out_w": 1280,
        "out_h": 720,
        "fps": 30,
        "crop_mode": "crop",
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
        # 16:9 for Discord/WebRTC; full 4:3 was blue-biased after bin2 + rejected by Electron.
        "out_w": 1920,
        "out_h": 1080,
        "fps": 30,
        "crop_mode": "crop",
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
        "out_w": 1280,
        "out_h": 720,
        "fps": 30,
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
        "out_w": 1280,
        "out_h": 720,
        "fps": 30,
        "crop_mode": "crop",
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
        "out_w": 1920,
        "out_h": 1080,
        "fps": 30,
        "crop_mode": "crop",
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
        "out_w": 1280,
        "out_h": 720,
        "fps": 30,
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
# Profile name while a V4L2 app (Discord, browser) holds the device.
# The tray icon watches PipeWire node state, which those apps never set.
INDICATOR = Path("/run/surface-webcam/indicator")
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
SKIP_COMMS = ("v4l2-ctl", "wireplumber", "pipewire")
# Soft readers (PipeWire publisher) hold loopbacks for exclusive_caps / PW enum
# but must NOT alone trigger sensor STREAMON / privacy LED.
_SOFT_CMD_MARKERS = (
    "surface.pw.",
    "surface-pw-publish",
    "surface-pw-bridge",
    "SURFACE_PW_PUBLISH=",
)
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
    """Resolve by card name — Front-Standard should be the lowest number (V4L2 default).

    Requires the 7-profile v4l2loopback layout.
    """
    found: dict[str, str] = {}
    try:
        ents = list(Path("/sys/class/video4linux").iterdir())
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
            found.setdefault("front-standard", f"/dev/{ent.name}")
        elif name == "Surface-Back":
            found.setdefault("back-standard", f"/dev/{ent.name}")
        pk = LABEL_TO_PROFILE.get(name)
        if pk:
            found[pk] = f"/dev/{ent.name}"
    missing = [k for k in PROFILE_ORDER if k not in found or not Path(found[k]).exists()]
    if missing:
        raise RuntimeError(
            "v4l2loopback missing profiles "
            f"{missing} (have={sorted(found)}). "
            "Reload: rmmod v4l2loopback; modprobe v4l2loopback "
            "devices=7 video_nr=60,61,62,63,64,65,66 "
            "card_label=Surface-Front-Standard,Surface-Front-HQ,Surface-Front-Fast,"
            "Surface-Back-Standard,Surface-Back-HQ,Surface-Back-Fast,Surface-IR-Howdy "
            "exclusive_caps=1,1,1,1,1,1,1"
        )
    return {k: found[k] for k in PROFILE_ORDER}


DEV = resolve_loopbacks()
_PW_CACHE = {"t": 0.0, "running": set()}
_PW_MIN_INTERVAL = 1.0  # Discord preview needs fast STREAMON after PW link


def _pw_xdg_runtime() -> str:
    """PipeWire lives in the graphical session — not root's /run/user/0."""
    if os.getuid() != 0:
        return os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    for sock in sorted(Path("/run/user").glob("*/pipewire-0")):
        return str(sock.parent)
    return "/run/user/1000"



def set_privacy_led(name: str, on: bool) -> None:
    p = LED.get(name)
    if p is None:
        return
    try:
        p.write_text("1\n" if on else "0\n")
    except OSError:
        pass


def privacy_leds_off() -> None:
    """Both RGB privacy LEDs off — idle / soft-only / no hard STREAMON."""
    set_privacy_led("front", False)
    set_privacy_led("back", False)


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
    # SB3 indoor (BGR). Keep G near 1.0 — mid-gw with Bayer-green midtones
    # previously locked G≈0.80 / B≈1.48 → strong magenta on skin/whites.
    _DEFAULT = {
        # Slightly cool vs the room tungsten so skin isn't yellow.
        "front": np.array([1.22, 0.94, 1.10], dtype=np.float32),
        "back": np.array([1.16, 0.95, 1.16], dtype=np.float32),
    }
    _DARK = {
        "front": np.array([1.16, 0.96, 1.06], dtype=np.float32),
        "back": np.array([1.10, 0.97, 1.12], dtype=np.float32),
    }

    def __init__(self, cam_tag: str = "") -> None:
        self.cam_tag = cam_tag
        self.lo: float | None = None
        self.hi: float | None = None
        self.scales: np.ndarray | None = None
        self._acc: list[tuple[np.ndarray, np.ndarray]] = []
        # Skip empty CSI dumps; need ≥2 usable frames before lock.
        self._need = 2
        self._skip_log_t = 0.0
        self._learn_n = 0
        self._raw_p99: float = 0.0
        self._raw_p50: float = 0.0
        self._default = self._DEFAULT.get(cam_tag, self._DEFAULT["front"]).copy()
        self._dark = self._DARK.get(cam_tag, self._DARK["front"]).copy()

    def ensure_scales(self) -> None:
        """Always have usable WB — never leave Bayer green / gray-world preview."""
        if self.scales is None:
            self.scales = self._default.copy()

    def _scales_for_brightness(self, p50: float) -> np.ndarray:
        """Lerp dark-neutral → locked/default as midtones rise (10-bit raw p50)."""
        base = self.scales if self.scales is not None else self._default
        # Below ~50: full dark profile; by ~160: full indoor/locked scales.
        t = float(np.clip((p50 - 45.0) / 120.0, 0.0, 1.0))
        return (self._dark * (1.0 - t) + base.astype(np.float32) * t).astype(np.float32)

    def learn(self, img16: np.ndarray, bgr: np.ndarray) -> None:
        sample = img16[::8, ::8] if img16.size > 200_000 else img16
        p50_now = float(np.percentile(sample, 50.0))
        self._raw_p50 = p50_now
        if self.scales is not None and not np.allclose(self.scales, self._default):
            # Relock check at most ~2 Hz — percentile on every frame was free GIL tax.
            self._learn_n += 1
            if self._learn_n % 15 != 0:
                return
            p50 = p50_now
            if self.lo is None or self.hi is None:
                return
            mid = (self.lo + self.hi) * 0.5
            # Only relock on drastic scene change — mid≈450 with p50≈130 used
            # to thrash every frame (blue↔green flicker across profiles).
            if mid > 1.0 and (p50 > mid * 3.5 or p50 < mid * 0.18):
                self.lo = self.hi = None
                # Dark relock → dark-neutral, not warm B boost.
                self.scales = (
                    self._dark.copy() if p50 < 100.0 else self._default.copy()
                )
                self._acc.clear()
                print(f"WB relock p50={p50:.0f} mid={mid:.0f}", flush=True)
            else:
                return
        self._learn_n += 1
        # While unlocked, sample every 3rd frame — empty CSI dumps spam learn().
        if self._learn_n % 3 != 0:
            return
        if img16.shape[0] >= 1500:
            img16 = img16[::4, ::4]
            bgr = bgr[::4, ::4]
        self._acc.append((img16.astype(np.float32), bgr.astype(np.float32)))
        if len(self._acc) < self._need:
            return
        raws = np.mean(np.stack([a[0] for a in self._acc], axis=0), axis=0)
        p1, p50, p95, p99, p995 = np.percentile(raws, (1.0, 50.0, 95.0, 99.0, 99.5))
        self._raw_p50 = float(p50)
        self._raw_p99 = float(p99)
        if p99 >= 980 and p1 >= 200:
            self._acc.clear()
            return
        # Empty/black CSI → reject; back indoor can be dark but still usable.
        thin_p99 = 90.0 if self.cam_tag == "back" else 180.0
        thin_span = 50.0 if self.cam_tag == "back" else 100.0
        thin_p50 = 18.0 if self.cam_tag == "back" else 40.0
        if p99 < thin_p99 or (p99 - p1) < thin_span or p50 < thin_p50:
            self._acc.clear()
            # Stay on dark-neutral while too thin to lock — avoids blue frame.
            if p50 < 90.0 or p99 < 160.0:
                self.scales = self._dark.copy()
            now = time.monotonic()
            if now - self._skip_log_t > 2.0:
                self._skip_log_t = now
                print(f"WB skip thin raw p1={p1:.0f} p50={p50:.0f} p99={p99:.0f}", flush=True)
            return
        # Floor hi: front stays wide; back follows real p99 so dark rooms aren't crushed.
        if self.cam_tag == "back":
            hi = float(max(min(p99 * 1.08, 950.0), min(max(p99, 160.0), 520.0)))
            lo = float(min(max(p1, 0.0), 48.0))
            if hi - lo < 120:
                hi = lo + 120.0
        else:
            # Prefer p95 over specular p99 so hi doesn't stick at 900.
            hi = float(max(min(max(p95, p99 * 0.85), 820.0), 280.0))
            lo = float(min(max(p1, 0.0), 64.0))
            if p995 > p95 * 2.5 and p95 >= 280:
                hi = float(max(min(p95 * 1.15, 820.0), 280.0))
            if hi - lo < 200:
                hi = lo + 200.0
        self.lo, self.hi = lo, hi
        stack = np.mean(np.stack([a[1] for a in self._acc], axis=0), axis=0)
        flat = stack.reshape(-1, 3)
        lum = flat.mean(axis=1)
        # Midtone gray-world first — clipped "whites" at 230,230,230 lock to
        # unity scales and leave the severe green cast untouched.
        mid_m = flat[(lum > 55.0) & (lum < 200.0)]
        if len(mid_m) >= 80:
            m = np.maximum(mid_m.mean(axis=0), 1.0)
            # Reference = mean of channels (not G-alone) so Bayer-green midtones
            # don't force B×1.5 / G×0.8 (magenta lock).
            gref = float(np.mean(m))
            gw = np.array(
                [
                    float(np.clip(gref / float(m[0]), 0.92, 1.35)),
                    float(np.clip(gref / float(m[1]), 0.88, 1.08)),
                    float(np.clip(gref / float(m[2]), 0.92, 1.32)),
                ],
                dtype=np.float32,
            )
        else:
            gw = self._default.copy()

        # Optional near-neutral highlight refine (reject specular clips).
        bright = flat[(lum > 140.0) & (lum < 220.0)]
        if len(bright) < 80:
            p90 = float(np.percentile(lum, 90.0))
            bright = flat[(lum >= max(100.0, p90 - 8.0)) & (lum < 220.0)]
        near_ok = False
        if len(bright) >= 40:
            mx = bright.max(axis=1)
            mn = bright.min(axis=1)
            chroma = mx - mn
            g_dom = (bright[:, 1] > bright[:, 0] + 12.0) | (bright[:, 1] > bright[:, 2] + 12.0)
            near = (chroma < 36.0) & (mn > mx * 0.72) & (~g_dom) & (mx < 215.0)
            candidates = bright[near]
            if len(candidates) >= 40:
                c_chroma = candidates.max(axis=1) - candidates.min(axis=1)
                c_lum = candidates.mean(axis=1)
                score = c_lum - 3.0 * c_chroma
                order = np.argsort(-score)
                take = candidates[order[: min(100, len(candidates))]]
                means = np.maximum(take.mean(axis=0), 1.0)
                g = float(means[1])
                learned = np.array(
                    [
                        float(np.clip(g / float(means[0]), 0.90, 2.00)),
                        1.0,
                        float(np.clip(g / float(means[2]), 0.90, 1.80)),
                    ],
                    dtype=np.float32,
                )
                # Prefer defaults + mild gw — avoid magenta from over-cut G.
                blended = (0.35 * gw + 0.20 * learned + 0.45 * self._default).astype(
                    np.float32
                )
                near_ok = True
                wp_means = means
            else:
                blended = (0.40 * gw + 0.60 * self._default).astype(np.float32)
                wp_means = m if len(mid_m) >= 80 else self._default
        else:
            blended = (0.40 * gw + 0.60 * self._default).astype(np.float32)
            wp_means = m if len(mid_m) >= 80 else self._default

        if not near_ok and len(mid_m) < 80:
            blended = self._dark.copy() if p50 < 110.0 else self._default.copy()
            print(f"WB default scales={blended} (no midtones p50={p50:.0f})", flush=True)
            self.scales = blended
            self._acc.clear()
            return

        blended[0] = float(np.clip(float(blended[0]), 1.02, 1.32))
        blended[1] = float(np.clip(float(blended[1]), 0.88, 1.02))
        blended[2] = float(np.clip(float(blended[2]), 1.02, 1.28))
        if self.cam_tag == "front":
            # Gray-world locks onto the yellow wall. A small cool bias on skin.
            blended[0] = float(np.clip(blended[0] * 1.035, 1.02, 1.36))
            blended[2] = float(np.clip(blended[2] * 0.965, 0.98, 1.28))
        # Magenta guard: B and R must not outrun G after lock.
        b_s, g_s, r_s = float(blended[0]), float(blended[1]), float(blended[2])
        if (b_s + r_s) * 0.5 > g_s + 0.22:
            pull = ((b_s + r_s) * 0.5 - g_s - 0.12) * 0.5
            blended[0] = float(max(1.02, b_s - pull))
            blended[2] = float(max(1.02, r_s - pull))
            blended[1] = float(min(1.02, g_s + pull * 0.5))
        self.scales = blended
        self._raw_p99 = float(p99)
        self._acc.clear()
        print(
            f"WB locked raw[{self.lo:.0f}..{self.hi:.0f}] scales={self.scales} "
            f"wp=({float(wp_means[0]):.0f},{float(wp_means[1]):.0f},{float(wp_means[2]):.0f})"
            f"{' mid-gw' if not near_ok else ''}",
            flush=True,
        )

    def stretch16(self, img16: np.ndarray) -> np.ndarray:
        return self.stretch_any(img16)

    def stretch_any(self, img: np.ndarray) -> np.ndarray:
        x = img
        large = img.size > 2_000_000
        sample = x[::8, ::8] if large else x
        if self.lo is None or self.hi is None:
            p1, p50, p99 = np.percentile(sample, (1.0, 50.0, 99.0))
            self._raw_p50 = float(p50)
            if p99 >= 980 and p1 >= 200:
                return cv2.convertScaleAbs(x, alpha=180.0 / 1023.0)
            # Empty CSI noise — keep linear (don't blow to white).
            if p99 < 70 and (p99 - p1) < 35:
                return cv2.convertScaleAbs(x, alpha=255.0 / 1023.0)
            # Dark-but-real scene — stretch real span (never force hi=420 on noise).
            if self.cam_tag == "back" or p99 < 280:
                lo = float(min(max(p1, 0.0), 40.0))
                hi = float(max(p99, lo + 80.0))
                peak = 235.0 if self.cam_tag == "back" else 210.0
                mid = max(hi - lo, 60.0)
                return cv2.convertScaleAbs(x, alpha=peak / mid, beta=-lo * peak / mid)
            lo, hi = float(p1), float(max(p99, 420.0))
        else:
            lo, hi = float(self.lo), float(self.hi)
            p99 = float(np.percentile(sample, 99.0))
            p50 = float(np.percentile(sample, 50.0))
            self._raw_p50 = p50
            # Shrink stretch window when scene went dark under an old lock.
            if p99 > 40 and hi > p99 * 1.5:
                hi = max(p99 * 1.12, lo + 100.0)
            if self.cam_tag == "back":
                if p99 > 40 and hi > p99 * 1.6:
                    hi = max(p99 * 1.1, lo + 100.0)
        if self.cam_tag == "back":
            lo = min(lo, 48.0)
            hi = max(hi, lo + 80.0)
            # Wider mapping window → less crushed shadows / fewer clipped whites.
            peak = 228.0
            mid = max(hi - lo, 100.0)
        else:
            lo = min(lo, 48.0)
            # Front: use real p99.5 as hi ceiling so specular doesn't force
            # hard clip of skin/pillow; keep lo low to lift shadows.
            p995 = float(np.percentile(sample, 99.5))
            p10 = float(np.percentile(sample, 10.0))
            if p995 > lo + 80:
                hi = max(min(max(hi, p995 * 1.02), 980.0), lo + 120.0)
            lo = min(lo, max(0.0, p10 * 0.85))
            if self._raw_p50 < 100.0:
                peak = 200.0
            else:
                peak = 210.0
            mid = max(hi - lo, 280.0)
        # Soft shoulder: map into peak with a little headroom so AE highlights
        # don't hard-clip to 255 after convertScaleAbs.
        out = cv2.convertScaleAbs(x, alpha=peak / mid, beta=-lo * peak / mid)
        return out

    def apply_wb_and_stretch(self, bgr16: np.ndarray) -> np.ndarray:
        if self.scales is None or self.lo is None or self.hi is None:
            bgr16 = self.apply_wb_linear(bgr16)
            return self.stretch_any(bgr16)

        # Update stretch window at ~3 Hz, not 30 Hz
        if not hasattr(self, "_stretch_cached") or self._learn_n % 10 == 0:
            sample = bgr16[::8, ::8]
            lo, hi = float(self.lo), float(self.hi)
            p99 = float(np.percentile(sample, 99.0))
            p50 = float(np.percentile(sample, 50.0))
            self._raw_p50 = p50
            if p99 > 40 and hi > p99 * 1.5:
                hi = max(p99 * 1.12, lo + 100.0)
            if self.cam_tag == "back":
                if p99 > 40 and hi > p99 * 1.6:
                    hi = max(p99 * 1.1, lo + 100.0)
                lo = min(lo, 48.0)
                hi = max(hi, lo + 80.0)
                peak = 228.0
            else:
                lo = min(lo, 48.0)
                p995 = float(np.percentile(sample, 99.5))
                p10 = float(np.percentile(sample, 10.0))
                if p995 > lo + 80:
                    hi = max(min(max(hi, p995 * 1.02), 980.0), lo + 120.0)
                lo = min(lo, max(0.0, p10 * 0.85))
                peak = 200.0 if self._raw_p50 < 100.0 else 210.0
            self._stretch_cached = (lo, hi, peak)
        else:
            lo, hi, peak = self._stretch_cached

        scales = self._scales_for_brightness(self._raw_p50)
        try:
            import surface_isp as _isp
            res = _isp.wb_stretch_u8(bgr16, scales, lo, hi, peak)
            if res is not None:
                return res
        except Exception:
            pass

        bgr16 = self.apply_wb_linear(bgr16)
        return self.stretch_any(bgr16)
    def apply_wb_linear(self, bgr16: np.ndarray) -> np.ndarray:
        if self.scales is None:
            return bgr16
        # Fade warm B boost in low light — pedestal/noise is otherwise cyan/blue.
        scales = self._scales_for_brightness(self._raw_p50)
        # Small black subtract before WB (10-bit pedestal); helps channel imbalance.
        ped = 12 if self._raw_p50 < 120.0 else 6
        out = np.empty_like(bgr16)
        for c in range(3):
            q = max(1, int(round(float(scales[c]) * 256.0)))
            ch = bgr16[:, :, c].astype(np.uint32)
            ch = np.where(ch > ped, ch - ped, 0)
            out[:, :, c] = np.minimum((ch * q) >> 8, 1023).astype(bgr16.dtype)
        return out


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
    """Fraction of lines that are real image, not CSI filler.

    A bright line starts with 0xFF and still has a picture further along.
    Judging only the first bytes dropped those frames, so exposure never
    moved and the preview stayed black.
    """
    rows = min(h, max(1, len(raw) // max(stride, 1)))
    step = max(1, rows // 64)
    good = 0
    n = 0
    for y in range(0, rows, step):
        n += 1
        start = y * stride + line_off
        end = (y + 1) * stride
        sample = raw[start:end:32]
        if sample and sample.count(0xFF) < int(len(sample) * 0.90):
            good += 1
    return good / float(max(n, 1))


def _bayer_crop_16x9(img16: np.ndarray) -> np.ndarray:
    """Center-crop Bayer to 16:9 with even coordinates (preserve CFA phase)."""
    h, w = img16.shape[:2]
    th = (w * 9 // 16) & ~1
    if th >= h or th < 2:
        return img16[: h & ~1, : w & ~1]
    y0 = ((h - th) // 2) & ~1
    return img16[y0 : y0 + th, : w & ~1]


def _bayer_rg_bin2_bgr(img16: np.ndarray) -> np.ndarray:
    """RGGB 2×2 → BGR u16 half-res (correct CFA bin; not naive ::2 subsample)."""
    h, w = img16.shape[:2]
    h2, w2 = h & ~1, w & ~1
    a = img16[:h2:2, :w2:2].astype(np.uint32)  # R
    b = img16[:h2:2, 1:w2:2].astype(np.uint32)  # G
    c = img16[1:h2:2, :w2:2].astype(np.uint32)  # G
    d = img16[1:h2:2, 1:w2:2].astype(np.uint32)  # B
    g = (b + c + 1) >> 1
    out = np.empty((h2 // 2, w2 // 2, 3), dtype=np.uint16)
    out[:, :, 0] = d
    out[:, :, 1] = g
    out[:, :, 2] = a
    return out


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
    fast: bool = False,
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

    # Prefer C MIPI unpack (≈10× vs NumPy); fall back to Python.
    img16: np.ndarray | None = None
    try:
        import surface_isp as _isp

        img16 = _isp.unpack_mipi10_u16(raw[:need], w, h, stride, line_off=line_off)
    except Exception:
        img16 = None
    if img16 is None:
        img16 = sc.unpack_mipi10_u16(raw[:need], w, h, stride, line_off=line_off)

    try:
        from pycompat import bayer_code, resize_area

        bayer = bayer_code(cv2, prefer_fast=True)
    except Exception:
        bayer = cv2.COLOR_BayerRG2BGR
        resize_area = lambda _cv, im, wh: cv2.resize(im, wh, interpolation=cv2.INTER_AREA)

    # HQ (≥2k) and Back-Standard (1632): Bayer 2×2 bin before tone.
    # SP7 SoftISP is HW (IPU3 IMGU); userspace full demosaic on ≥1.6MP
    # cannot hold 30 unique_fps. Bin → tone → upscale once.
    hq = (not fast) and w >= 2000
    med = (not fast) and (not hq) and w >= 1600
    if hq or med:
        bgr16 = None
        try:
            import surface_isp as _isp

            bgr16 = _isp.bayer_bin2_bgr(img16, crop_16x9=False)
        except Exception:
            bgr16 = None
        if bgr16 is None:
            bgr16 = _bayer_rg_bin2_bgr(img16)
        # Full-res modes: RGGB 2×2 bin is opposite CFA phase vs OpenCV BayerRG
        # on the binned 1296/1632 modes → strong blue cast on Front/Back-HQ.
        bgr16 = np.ascontiguousarray(bgr16[:, :, ::-1])
        bgr16 = sc._orient(bgr16, cam)
        img16 = bgr16[:, :, 1]  # tone learn proxy (no copy)
        # HQ: leave ~1920×1440 then fit_frame crops to 1920×1080.
        native_wh = (1920, 1440) if hq else None
    else:
        try:
            bgr16 = cv2.cvtColor(img16, bayer)
        except cv2.error:
            bgr16 = cv2.cvtColor(img16, cv2.COLOR_BayerRG2BGR)
        bgr16 = sc._orient(bgr16, cam)
        native_wh = None

    # Learn only needs a cheap stretch preview while unlocked.
    if tone.scales is None or np.allclose(tone.scales, tone._default):
        tone.learn(img16, tone.stretch_any(bgr16))
        tone.ensure_scales()
        bgr16 = tone.apply_wb_linear(bgr16)
        bgr = tone.stretch_any(bgr16)
    else:
        tone.learn(img16, bgr16)  # locked path samples sparsely
        tone.ensure_scales()
        bgr = tone.apply_wb_and_stretch(bgr16)
    if native_wh is not None:
        tw, th = native_wh
        if bgr.shape[1] != tw or bgr.shape[0] != th:
            bgr = resize_area(cv2, bgr, (tw, th))
    if fast:
        bh, bw = bgr.shape[:2]
        th = (bw * 9 // 16) & ~1
        if 2 <= th < bh:
            y0 = ((bh - th) // 2) & ~1
            bgr = bgr[y0 : y0 + th]
        if bgr.shape[0] >= 400 and bgr.shape[1] >= 600:
            nh, nw = bgr.shape[0] // 2, bgr.shape[1] // 2
            bgr = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_AREA)
    hh, ww = bgr.shape[:2]
    scale = 0.5 if fast and w >= 800 else 1.0
    r = min(int(crop_r * scale), max(ww - 8, 0))
    btm = min(int(crop_b * scale), max(hh - 8, 0))
    left = min(int(crop_l * scale), max(ww - 8, 0))
    if r or btm or left:
        bgr = bgr[0 : hh - btm, left : ww - r]
    # White neut only while WB still on defaults — float32 full frame ≈100ms.
    locked = tone.scales is not None and not np.allclose(tone.scales, tone._default)
    if cam.tag in ("front", "back") and not locked and not fast:
        s = bgr[::8, ::8]
        mx = s.max(axis=2)
        mn = s.min(axis=2)
        if np.any((mx > 210) & ((mx - mn) < 22)):
            f = bgr.astype(np.float32)
            mx = f.max(axis=2)
            mn = f.min(axis=2)
            white = (mx > 210) & ((mx - mn) < 22)
            if np.any(white):
                avg = f[white].mean(axis=1, keepdims=True)
                f[white] = f[white] * 0.55 + avg * 0.45
                bgr = np.clip(f, 0, 255).astype(np.uint8)
    if cam.tag == "back":
        # Mean on subsample; avoid float32 full-frame alloc on every frame
        # (was crushing Back-Standard unique_fps to ~9).
        grey_s = cv2.cvtColor(bgr[::4, ::4], cv2.COLOR_BGR2GRAY)
        mean = float(grey_s.mean())
        if mean < 70.0 and not fast and not hq and not med:
            ycrcb = cv2.cvtColor(bgr, cv2.COLOR_BGR2YCrCb)
            clahe = cv2.createCLAHE(
                clipLimit=2.0 if mean < 45 else 1.5, tileGridSize=(8, 8)
            )
            ycrcb[:, :, 0] = clahe.apply(ycrcb[:, :, 0])
            bgr = cv2.cvtColor(ycrcb, cv2.COLOR_YCrCb2BGR)
            boost = 1.08 + max(0.0, (70.0 - mean) / 70.0) * 0.45
            bgr = cv2.convertScaleAbs(bgr, alpha=boost, beta=6.0)
        elif mean < 85.0:
            bgr = cv2.convertScaleAbs(bgr, alpha=1.10, beta=4.0)
        else:
            # Mild local contrast — lift shadows without blowing window.
            ycrcb = cv2.cvtColor(bgr, cv2.COLOR_BGR2YCrCb)
            clahe = cv2.createCLAHE(clipLimit=1.25, tileGridSize=(8, 8))
            ycrcb[:, :, 0] = clahe.apply(ycrcb[:, :, 0])
            bgr = cv2.cvtColor(ycrcb, cv2.COLOR_YCrCb2BGR)
    elif cam.tag == "front" and not fast:
        # Front: ultra-fast shadow lift & contrast (2ms vs 217ms in LAB / 25ms CLAHE)
        bgr = cv2.convertScaleAbs(bgr, alpha=1.05, beta=3.0)

    # Residual Bayer green (esp. HQ) — only when clearly green, not when magenta.
    s = bgr[::8, ::8].astype(np.float32)
    lum = s.mean(axis=2)
    mid = s[(lum > 40.0) & (lum < 200.0)]
    if len(mid) >= 40:
        mb, mg, mr = mid.mean(axis=0)
        gr = float(mg - mr)
        gb = float(mg - mb)
        mag = float((mr + mb) * 0.5 - mg)
        if mag > 6.0:
            # Magenta cast (screens 1–2): lift G, ease R/B via cv2.transform (4ms vs 66ms)
            g_mul = float(np.clip(1.0 + 0.010 * mag, 1.0, 1.18))
            r_mul = float(np.clip(1.0 - 0.007 * mag, 0.82, 1.0))
            b_mul = float(np.clip(1.0 - 0.008 * mag, 0.80, 1.0))
            M = np.diag([b_mul, g_mul, r_mul])
            bgr = cv2.transform(bgr, M)
        elif gr > 6.0 or gb > 8.0:
            g_mul = float(np.clip(1.0 - 0.008 * max(gr, 0.0) - 0.004 * max(gb, 0.0), 0.88, 1.0))
            r_mul = float(np.clip(1.0 + 0.005 * max(gr, 0.0), 1.0, 1.10))
            b_mul = float(np.clip(1.0 + 0.003 * max(gr, 0.0) - 0.004 * max(gb, 0.0), 0.94, 1.06))
            M = np.diag([b_mul, g_mul, r_mul])
            bgr = cv2.transform(bgr, M)

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


def _loopback_fourcc(profile: str) -> str:
    return "YUYV"


def _open_loopback(dev: str, w: int, h: int, fps: int = 30, fourcc: str = "YUYV") -> int:
    last_err: OSError | None = None
    fps = max(1, int(fps))
    # Readers holding exclusive_caps: open writer only — never S_FMT / set-parm
    # (guvcview does S_FMT on CAPTURE and SIGSEGVs if we race the format).
    try:
        if readers_of(dev):
            fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
            try:
                flg = fcntl.fcntl(fd, fcntl.F_GETFL)
                fcntl.fcntl(fd, fcntl.F_SETFL, flg | os.O_NONBLOCK)
            except OSError:
                pass
            if _fd_has_capture(fd):
                _arm_mmap_out(fd, w * h * 2, dev)
                return fd
            os.close(fd)
            # Reader present but no CAPTURE yet — wait, do not touch format.
            raise OSError(errno.EBUSY, "reader holds loopback")
    except OSError as e:
        if getattr(e, "errno", None) == errno.EBUSY:
            raise
    for attempt in range(3):
        # OUTPUT format only. Never --set-fmt-video (CAPTURE) — that is what
        # guvcview also does and the race kills it. Never --get-fmt-video either
        # (opens CAPTURE side).
        try:
            # OUTPUT format only (profile sizes, e.g. Front-Fast 1296x728).
            subprocess.run(
                ["v4l2-ctl", "-d", dev, "-c", "keep_format=0"],
                capture_output=True,
                timeout=2,
            )
        except Exception:
            pass
        try:
            r = subprocess.run(
                [
                    "v4l2-ctl",
                    "-d",
                    dev,
                    "--set-fmt-video-out",
                    # 16:9 only. 4:3 YU12 was the green-static case, not the fourcc.
                    f"width={w},height={h},pixelformat={fourcc}",
                ],
                capture_output=True,
                timeout=2,
                text=True,
            )
            if r.returncode != 0:
                err = (r.stderr or r.stdout or "").strip()
                print(f"loopback fmt {dev} {fourcc} {w}x{h} failed: {err}", flush=True)
        except Exception as e:
            print(f"loopback fmt {dev} {e}", flush=True)
        # Discord/Chromium drop devices with fps=0 or broken timeperframe
        # (Front-Fast was advertising 1/-1 → only ~4 cams visible).
        try:
            subprocess.run(
                ["v4l2-ctl", "-d", dev, f"--set-parm={fps}"],
                capture_output=True,
                timeout=2,
            )
        except Exception:
            pass
        try:
            subprocess.run(
                ["v4l2-ctl", "-d", dev, "-c", "keep_format=1"],
                capture_output=True,
                timeout=2,
            )
        except Exception:
            pass
        try:
            # All 7 Surface loopbacks (incl. IR) must be world-open for apps.
            # Howdy exclusivity is process-level (enforce_ir_mutex), not chmod.
            os.chmod(dev, 0o666)
        except OSError:
            pass
        # Bail if a reader appeared while we were setting format.
        if readers_of(dev):
            raise OSError(errno.EBUSY, "reader appeared during format set")
        try:
            fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
        except OSError as e:
            last_err = e
            time.sleep(0.12 * (attempt + 1))
            continue
        try:
            flg = fcntl.fcntl(fd, fcntl.F_GETFL)
            fcntl.fcntl(fd, fcntl.F_SETFL, flg | os.O_NONBLOCK)
        except OSError:
            pass
        if _fd_has_capture(fd):
            _arm_mmap_out(fd, w * h * 2, dev)
            return fd
        os.close(fd)
        time.sleep(0.12 * (attempt + 1))
    if last_err:
        raise last_err
    raise OSError("loopback open without CAPTURE")


def _write_yuyv(fd: int, yuyv: bytes) -> int:
    """Queue a full frame. write() races Discord's mmap CaptureThread (SIGSEGV)."""
    sink = _MMAP_OUT.get(fd)
    if sink is not None:
        try:
            return sink.write(yuyv)
        except OSError:
            return -1
    total = 0
    view = memoryview(yuyv)
    while total < len(yuyv):
        try:
            n = os.write(fd, view[total:])
        except BlockingIOError:
            if total == 0:
                return 0
            return -1
        except OSError:
            return -1
        if n <= 0:
            return -1
        total += n
    return total


_V4L2_BUF_TYPE_VIDEO_OUTPUT = 2
_V4L2_MEMORY_MMAP = 1
_VIDIOC_REQBUFS = 0xC0145608
_VIDIOC_QUERYBUF = 0xC0585609
_VIDIOC_QBUF = 0xC058560F
_VIDIOC_DQBUF = 0xC0585611
_VIDIOC_STREAMON = 0x40045612


class _MmapOut:
    """OUTPUT queue. Discord DQBUFs these buffers; write() was overwriting them."""

    def __init__(self, fd: int, nbytes: int) -> None:
        self.fd = fd
        self.nbytes = nbytes
        self.maps: list[mmap.mmap] = []
        self.free: list[int] = []
        # Discord maps at most 4 buffers and then indexes DQBUF blindly.
        # A leftover allocation of 8 (max_buffers) returns index >= 4 and
        # CaptureThread SIGSEGVs. Drop any previous queue, then ask for 4.
        zero = bytearray(struct.pack("IIIII", 0, _V4L2_BUF_TYPE_VIDEO_OUTPUT, _V4L2_MEMORY_MMAP, 0, 0))
        try:
            fcntl.ioctl(fd, _VIDIOC_REQBUFS, zero)
        except OSError:
            pass
        req = bytearray(struct.pack("IIIII", 4, _V4L2_BUF_TYPE_VIDEO_OUTPUT, _V4L2_MEMORY_MMAP, 0, 0))
        fcntl.ioctl(fd, _VIDIOC_REQBUFS, req)
        count = struct.unpack_from("I", req, 0)[0]
        if count < 2 or count > 4:
            raise OSError(f"REQBUFS count {count} (Discord maps only 4)")
        for i in range(count):
            b = bytearray(88)
            struct.pack_into("I", b, 0, i)
            struct.pack_into("I", b, 4, _V4L2_BUF_TYPE_VIDEO_OUTPUT)
            struct.pack_into("I", b, 60, _V4L2_MEMORY_MMAP)
            fcntl.ioctl(fd, _VIDIOC_QUERYBUF, b)
            length = struct.unpack_from("I", b, 72)[0]
            offset = struct.unpack_from("I", b, 64)[0]
            if length < nbytes:
                raise OSError(f"buffer {length} < frame {nbytes}")
            mm = mmap.mmap(
                fd, length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=offset
            )
            self.maps.append(mm)
            self.free.append(i)
        fcntl.ioctl(fd, _VIDIOC_STREAMON, struct.pack("I", _V4L2_BUF_TYPE_VIDEO_OUTPUT))

    def write(self, frame: bytes) -> int:
        if len(frame) != self.nbytes:
            return -1
        if not self.free:
            b = bytearray(88)
            struct.pack_into("I", b, 4, _V4L2_BUF_TYPE_VIDEO_OUTPUT)
            struct.pack_into("I", b, 60, _V4L2_MEMORY_MMAP)
            try:
                fcntl.ioctl(self.fd, _VIDIOC_DQBUF, b)
            except OSError:
                return 0
            self.free.append(struct.unpack_from("I", b, 0)[0])
        idx = self.free.pop()
        mm = self.maps[idx]
        mm.seek(0)
        mm.write(frame)
        b = bytearray(88)
        struct.pack_into("I", b, 0, idx)
        struct.pack_into("I", b, 4, _V4L2_BUF_TYPE_VIDEO_OUTPUT)
        struct.pack_into("I", b, 8, self.nbytes)
        struct.pack_into("I", b, 60, _V4L2_MEMORY_MMAP)
        ts = time.clock_gettime(time.CLOCK_MONOTONIC)
        sec = int(ts)
        struct.pack_into("qq", b, 24, sec, int((ts - sec) * 1_000_000))
        fcntl.ioctl(self.fd, _VIDIOC_QBUF, b)
        return self.nbytes

    def drain(self) -> None:
        """Return queued frames to the free list so a new opener blocks in select()."""
        for _ in range(len(self.maps) + 1):
            b = bytearray(88)
            struct.pack_into("I", b, 4, _V4L2_BUF_TYPE_VIDEO_OUTPUT)
            struct.pack_into("I", b, 60, _V4L2_MEMORY_MMAP)
            try:
                fcntl.ioctl(self.fd, _VIDIOC_DQBUF, b)
            except OSError:
                return
            idx = struct.unpack_from("I", b, 0)[0]
            if idx in self.free or not (0 <= idx < len(self.maps)):
                return
            self.free.append(idx)

    def close(self) -> None:
        for mm in self.maps:
            try:
                mm.close()
            except BufferError:
                pass
        self.maps.clear()


_MMAP_OUT: dict[int, _MmapOut] = {}


def _arm_mmap_out(fd: int, nbytes: int, dev: str) -> None:
    old = _MMAP_OUT.pop(fd, None)
    if old is not None:
        old.close()
    try:
        _MMAP_OUT[fd] = _MmapOut(fd, nbytes)
        print(f"mmap out {dev} {nbytes} bufs={len(_MMAP_OUT[fd].maps)}", flush=True)
    except OSError as e:
        print(f"mmap out {dev} fallback write: {e}", flush=True)


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


def _frame_bytes(profile: str) -> int:
    ow, oh = out_size(profile)
    return ow * oh * 2  # YUYV


def loading_yuyv(profile: str = "front-standard") -> bytes:
    """Solid dark YUYV. No caption — Discord mirrors the first frame and a
    'loading' label sticks in the preview when the next frame fails to convert."""
    ow, oh = out_size(profile)
    key = (ow, oh, "yuyv")
    cached = _LOADING.get(key)
    if cached is not None:
        return cached
    bgr = np.full((oh, ow, 3), 12, dtype=np.uint8)
    frame = sc.bgr_or_grey_to_yuyv(bgr)
    _LOADING[key] = frame
    return frame


def load_hold_frames() -> dict[str, bytes]:
    try:
        for p in CACHE_DIR.glob("*.yuyv"):
            p.unlink()
        for p in CACHE_DIR.glob("*.yu12"):
            p.unlink()
        for p in CACHE_DIR.glob("*.rgb3"):
            p.unlink()
    except OSError:
        pass
    return {n: loading_yuyv(n) for n in PROFILE_ORDER}


def save_hold_frame(name: str, yuyv: bytes) -> None:
    return


_PW_NICK_TO_PROFILE = {
    "Front-Standard": "front-standard",
    "Front-HQ": "front-hq",
    "Front-Fast": "front-fast",
    "Back-Standard": "back-standard",
    "Back-HQ": "back-hq",
    "Back-Fast": "back-fast",
}


def _profile_from_pw_props(props: dict) -> str | None:
    """Map PipeWire camera node props → profile key."""
    nick = props.get("node.nick") or ""
    if nick in _PW_NICK_TO_PROFILE:
        return _PW_NICK_TO_PROFILE[nick]
    name = props.get("node.name") or ""
    if name.startswith("surface.pw."):
        return _PW_NICK_TO_PROFILE.get(name.split(".", 2)[-1])
    desc = props.get("node.description") or ""
    for key, cfg in PROFILES.items():
        if cfg.get("label") == desc:
            return key
    path = props.get("api.v4l2.path") or ""
    for key, dev in DEV.items():
        base = dev.rsplit("/", 1)[-1]
        if path == dev or path.endswith(base):
            return key
    return None


def pw_running_cams() -> set[str]:
    """Profiles whose PipeWire Video/Source is actively streaming (client linked)."""
    now = time.time()
    if now - _PW_CACHE["t"] < _PW_MIN_INTERVAL:
        return set(_PW_CACHE["running"])
    xdg = _pw_xdg_runtime()
    env = os.environ.copy()
    env["XDG_RUNTIME_DIR"] = xdg
    env["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={xdg}/bus"
    running: set[str] = set()
    try:
        raw = subprocess.run(
            ["pw-dump"],
            capture_output=True,
            timeout=0.8,
            env=env,
        ).stdout
        if raw:
            for o in json.loads(raw):
                info = o.get("info") or {}
                p = info.get("props") or {}
                if p.get("media.class") != "Video/Source":
                    continue
                # idle/suspended = enumerated only; running = app capturing
                if info.get("state") != "running":
                    continue
                key = _profile_from_pw_props(p)
                if key:
                    running.add(key)
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError, ValueError):
        running = set(_PW_CACHE["running"])
    _PW_CACHE["t"] = now
    _PW_CACHE["running"] = running
    return set(running)


def _cmdline_is_soft_reader(cmd: str) -> bool:
    return any(m in cmd for m in _SOFT_CMD_MARKERS)


_READERS_CACHE: dict[str, object] = {"t": 0.0, "map": {}, "soft": {}}
_READERS_TTL = 0.5  # fast polling for responsive stream on/off

# Inactivity Timeout (2.0s) & Grace Period (2.0s):
# Tracks readers across ALL applications (Discord, Cheese, Chrome, Firefox, OBS, etc.).
# For read() users: monitors rchar and syscr.
# For mmap() users (Chromium/Electron/Qt): monitors wchar (IPC) and utime+stime (CPU decode/render ticks).
_READER_TRACKER: dict[int, tuple[int, int, int, int, int, float, float]] = {}  # pid -> (rc, sc, wc, ut, st, last_act, first_seen)
_INACTIVITY_TIMEOUT_S = 2.0
# STREAMON setup blocks the opener in DQBUF for ~2s with no /proc io.
# A 2s grace expired in that window, the reader looked idle, and the
# daemon STREAMOFF'd — the preview then stayed on zero buffers.
_INACTIVITY_GRACE_S = 5.0


def _pid_activity_stats(pid: int) -> tuple[int, int, int, int, int] | None:
    rc, sc, wc = 0, 0, 0
    ut, st = 0, 0
    try:
        with open(f"/proc/{pid}/io", "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("rchar:"):
                    rc = int(line.split()[1])
                elif line.startswith("syscr:"):
                    sc = int(line.split()[1])
                elif line.startswith("wchar:"):
                    wc = int(line.split()[1])
    except (OSError, ValueError):
        pass

    try:
        with open(f"/proc/{pid}/stat", "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
            rparen = content.rfind(")")
            if rparen != -1:
                fields = content[rparen + 2:].split()
                ut = int(fields[11])
                st = int(fields[12])
    except (OSError, ValueError, IndexError):
        pass

    if rc == 0 and sc == 0 and wc == 0 and ut == 0 and st == 0:
        return None
    return rc, sc, wc, ut, st


def _is_reader_active(pid: int, cmd: str) -> bool:
    """Determine if a reader PID is actively consuming frames or dormant."""
    global _READER_TRACKER
    now = time.monotonic()

    if _cmd_looks_like_howdy(cmd):
        return True

    cmd_lower = cmd.lower()
    if any(
        x in cmd_lower
        for x in (
            "discord",
            "chrome",
            "chromium",
            "firefox",
            "teams",
            "zoom",
            "obs",
            "kamoso",
            "cheese",
            "vlc",
            "ffmpeg",
            "gstreamer",
            "gst",
        )
    ):
        return True

    stats = _pid_activity_stats(pid)
    if stats is None:
        return True

    rc, sc, wc, ut, st = stats
    prev = _READER_TRACKER.get(pid)
    if prev is None:
        _READER_TRACKER[pid] = (rc, sc, wc, ut, st, now, now)
        return True

    old_rc, old_sc, old_wc, old_ut, old_st, last_act, first_seen = prev

    has_progress = (
        (rc > old_rc)
        or (sc > old_sc)
        or (wc > old_wc + 512)
        or ((ut + st) > (old_ut + old_st))
    )

    if has_progress:
        _READER_TRACKER[pid] = (rc, sc, wc, ut, st, now, first_seen)
        return True

    if (now - first_seen) < _INACTIVITY_GRACE_S:
        _READER_TRACKER[pid] = (rc, sc, wc, ut, st, last_act, first_seen)
        return True

    if (now - last_act) < _INACTIVITY_TIMEOUT_S:
        return True

    _READER_TRACKER[pid] = (rc, sc, wc, ut, st, last_act, first_seen)
    return False


def readers_of(dev: str) -> set[int]:
    """Hard readers only (apps). Soft PW-publisher pids are excluded."""
    m = _READERS_CACHE.get("map")
    if isinstance(m, dict) and m:
        return set(m.get(dev) or ())
    m = refresh_readers_map()
    return set(m.get(dev) or ())


def soft_readers_of(dev: str) -> set[int]:
    m = _READERS_CACHE.get("soft")
    if isinstance(m, dict):
        return set(m.get(dev) or ())
    return set()


def refresh_readers_map(force: bool = False) -> dict[str, set[int]]:
    """One /proc walk for all loopback devices. Prefer cached map (background refresher)."""
    global DEV, _CHEESE_IO
    now = time.monotonic()
    cache_map = _READERS_CACHE.get("map")
    if (
        not force
        and isinstance(cache_map, dict)
        and cache_map
        and now - float(_READERS_CACHE["t"]) < _READERS_TTL
    ):
        return cache_map  # type: ignore[return-value]
    me = os.getpid()
    wanted = {dev: set() for dev in DEV.values() if dev}
    soft_wanted = {dev: set() for dev in DEV.values() if dev}
    real_of = {}
    for dev in wanted:
        try:
            real_of[dev] = os.path.realpath(dev)
        except OSError:
            real_of[dev] = dev
    bases = {dev: dev.rsplit("/", 1)[-1] for dev in wanted}
    try:
        pids = os.listdir("/proc")
    except OSError:
        return wanted
    readers_seen: set[int] = set()
    for name in pids:
        if not name.isdigit():
            continue
        p = int(name)
        if p == me:
            continue
        pid = Path("/proc") / name
        try:
            with open(pid / "comm", "r", encoding="utf-8", errors="ignore") as f:
                comm = f.read().strip()
        except OSError:
            continue
        if comm in SKIP_COMMS:
            continue
        try:
            with open(pid / "cmdline", "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode("utf-8", "ignore")
        except OSError:
            cmd = ""
        if "surface_webcamd" in cmd or "surface_webcam_daemon" in cmd:
            continue
        soft = _cmdline_is_soft_reader(cmd)
        fd_dir = f"/proc/{name}/fd"
        try:
            fds = os.listdir(fd_dir)
        except OSError:
            continue
        for fdn in fds:
            try:
                t = os.readlink(f"{fd_dir}/{fdn}")
            except OSError:
                continue
            for dev in wanted:
                if t == dev or t.endswith(bases[dev]) or t == real_of[dev]:
                    readers_seen.add(p)
                    is_soft = soft or (not _is_reader_active(p, cmd))
                    if is_soft:
                        soft_wanted[dev].add(p)
                    else:
                        wanted[dev].add(p)
                    break
    for p in list(_READER_TRACKER.keys()):
        if p not in readers_seen:
            _READER_TRACKER.pop(p, None)
    _READERS_CACHE["t"] = now
    _READERS_CACHE["map"] = wanted
    _READERS_CACHE["soft"] = soft_wanted
    return wanted


def start_readers_refresher(stop: threading.Event) -> None:
    """Background /proc walker — never run walks on CFR/decode/main GIL hot paths."""

    def _run() -> None:
        while not stop.is_set():
            try:
                refresh_readers_map(force=True)
            except Exception:
                pass
            sleep_time = _READERS_TTL
            stop.wait(sleep_time)

    threading.Thread(target=_run, daemon=True, name="readers-refresh").start()


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
        "surface-howdy",  # enroll helpers + hold scripts
        "howdy --user",
        "howdy -u",
        "howdy add",
        "howdy test",
        "howdy clear",
        "howdy/compare.py",
        " howdy ",
        "/howdy ",
        "/howdy\0",
    )
    if any(n in c for n in needles):
        return True
    # Bare argv0 == howdy (PAM / CLI)
    toks = c.replace("\0", " ").split()
    return bool(toks) and (toks[0] == "howdy" or toks[0].endswith("/howdy"))


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


def _pids_are_call_app(pids: set[int]) -> bool:
    """Discord/Zoom/Teams/browsers — prefer their open profile for ISYS."""
    for pid in pids:
        try:
            cmd = Path(f"/proc/{pid}/cmdline").read_bytes().lower()
        except OSError:
            continue
        if any(
            x in cmd
            for x in (
                b"discord",
                b"zoom",
                b"teams",
                b"chrome",
                b"msedge",
                b"chromium",
                b"firefox",
            )
        ):
            return True
    return False


def _best_open_profile(cam: str, open_set: set[str], current: str = "idle") -> str | None:
    """Pick which profile to capture for a camera family.

    Prefer sticky current if still open (avoid ISYS thrash on Discord enum).
    Else: call-app readers (Discord/Chrome) beat preview apps (Kamoso/Cheese),
    then hq > standard > fast.
    """
    cands = [p for p in profiles_of(cam) if p in open_set]
    if not cands:
        return None
    rank = {"hq": 3, "standard": 2, "fast": 1}
    rmap = _READERS_CACHE.get("map") if isinstance(_READERS_CACHE.get("map"), dict) else {}

    def _rank(p: str) -> int:
        base = rank.get(p.rsplit("-", 1)[-1], 0)
        pids = set(rmap.get(DEV.get(p, ""), ()) or ())
        if _pids_are_call_app(pids):
            base += 10
        return base

    best = max(cands, key=_rank)
    if current in cands and _rank(current) >= _rank(best):
        return current
    return best


# Discord's device enum opens every /dev/video6N for a few hundred ms.
# Switching ISYS on that blip (idle → Front-HQ at 2592x1944) drops the
# Standard preview the user actually selected, so it stays on Loading.
_OPEN_SINCE: dict[str, float] = {}
_OPEN_STABLE_S = 0.7


def _stable_profiles(rmap: dict) -> set[str]:
    now = time.monotonic()
    seen: set[str] = set()
    for pk, dev in DEV.items():
        if rmap.get(dev):
            seen.add(pk)
            _OPEN_SINCE.setdefault(pk, now)
        else:
            _OPEN_SINCE.pop(pk, None)
    return {pk for pk in seen if now - _OPEN_SINCE.get(pk, now) >= _OPEN_STABLE_S}


def pick_wanted(current: str, has_seed: bool) -> str:
    # Cache only — background thread refreshes; never force a /proc walk here.
    rmap = _READERS_CACHE.get("map") if isinstance(_READERS_CACHE.get("map"), dict) else {}
    if not rmap:
        rmap = refresh_readers_map()
    pw = pw_running_cams()
    open_set = _stable_profiles(rmap)
    for pk in pw:
        if pk in PROFILES:
            open_set.add(pk)
    ir_open = "ir" in open_set
    front_p = _best_open_profile("front", open_set, current)
    back_p = _best_open_profile("back", open_set, current)
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
    # IR wins only when it is the sole open camera family. Discord/Chrome probe
    # and pipewiresink bridges must never yank ISYS onto IR while RGB is open.
    if front_p or back_p:
        # Prefer RGB whenever any Front/Back profile has a reader.
        pass
    elif IR_ENABLED and ir_open:
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
    """True if target profile can be produced from active capture (same sensor family)."""
    return family(active) == family(target)


class Capture:
    def __init__(self) -> None:
        self.stop = threading.Event()
        self.cap: subprocess.Popen | None = None
        self.name = "idle"
        self.last_yuyv: dict[str, bytes | None] = {k: None for k in PROFILE_ORDER}
        self._real_frames: set[str] = set()
        self.stats = {"got": 0, "drop": 0, "dec": 0, "out": 0}
        self._threads: list[threading.Thread] = []
        self._from_back = False
        self._front_retries = 0
        self._hold_only = False
        self._ir_halt_block_until = 0.0
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
        self._ae_pending: tuple[str, int, int] | None = None

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
            self._real_frames.add(pk)

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
                    timeout=3,
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
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
                try:
                    Path("/run/surface-ipu").mkdir(parents=True, exist_ok=True)
                    Path("/run/surface-ipu/isys-dead").write_text(
                        f"{time.strftime('%Y-%m-%dT%H:%M:%S')} open fail {e}\n"
                    )
                except OSError:
                    pass
                raise RuntimeError(f"ISYS dead before {name} STREAMON: {e}") from e
        dev = sc.media_setup(cam, cfg["cap_w"], cfg["cap_h"])
        sc.set_sensor_exposure(sensor, cam.exposure, cam.gain)
        if cam_key == "front":
            sub = sc.find_subdev("ov5693")
            if sub:
                # 60 fps for Front-Fast (vblank=50); 400 for Standard/HQ 30 fps
                vblank = 50 if cfg.get("fps", 30) >= 60 else 400
                subprocess.run(
                    [
                        "v4l2-ctl",
                        "-d",
                        sub,
                        "--set-ctrl",
                        f"vertical_blanking={vblank}",
                    ],
                    capture_output=True,
                )
        if cam_key == "back":
            self._front_retries = 0
            try:
                BACK_USED.unlink(missing_ok=True)
            except OSError:
                pass
            sub = sc.find_subdev("ov8865")
            if sub:
                # 60 fps for Back-Fast (800x600 @ vblank=320); 1200 for Standard/HQ 30 fps
                vblank = 320 if cfg.get("fps", 30) >= 60 else 1200
                subprocess.run(
                    [
                        "v4l2-ctl",
                        "-d",
                        sub,
                        "--set-ctrl",
                        f"vertical_blanking={vblank},red_balance={sc.OV8865_RED_BALANCE},"
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
                # Path is enough to attempt AF. Hold-open often EINVAL on SB3;
                # set_focus still works via short-lived open / v4l2-ctl once
                # ov8865 is streaming (upstream SoftISP does the same wait).
                if lens:
                    sc.set_focus(self._af_best)
                    self._af_pos = self._af_best
                    if is_hq:
                        print("AF: HQ — mid focus, micro-hunt only", flush=True)
                        self._af_phase = "track"
                        self._af_last_t = time.monotonic()
                        self._af_track_iv = 4.0
                    else:
                        print("AF: coarse sweep + track (DW9719)", flush=True)
                        self._af_phase = "coarse"
                        self._af_track_iv = 2.5
                else:
                    print(
                        "AF: DW9719 not found — Back streams without focus hunt",
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
                    cap = self.cap
                    if cap is not None and cap.stdout:
                        try:
                            while cap.stdout.read(256):
                                pass
                        except BlockingIOError:
                            pass
                        except Exception:
                            pass
                    time.sleep(0.0005)
                    continue
                # Pipe fallback (old raw_hold)
                cap = self.cap
                try:
                    chunk = (
                        cap.stdout.read(65536)
                        if cap is not None and cap.stdout
                        else b""
                    )
                except BlockingIOError:
                    chunk = b""
                if not chunk:
                    if cap is not None and cap.poll() is not None:
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
            ae_interval = 4.0 if is_hq else 2.5
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
                        fast=bool(name.endswith("-fast")),
                    )
                    if bgr is None:
                        # A blown frame is almost all 0xFF, so it fails the
                        # same test as an empty CSI buffer. Dropping it means
                        # AE never runs, exposure stays at the cold-start
                        # value, and the app repeats one black buffer until
                        # some other client happens to get a frame through.
                        if cam_key in ("front", "back"):
                            now_c = time.monotonic()
                            sample = raw[::128]
                            n_s = len(sample)
                            ff = (sample.count(0xFF) / n_s) if n_s else 0.0
                            exp_lo = sc.ae_limits_for(sensor)[0]
                            if (
                                ff >= 0.50
                                and ae_exp > exp_lo
                                and now_c - ae_last_t >= 0.35
                            ):
                                ae_last_t = now_c
                                new_exp = max(exp_lo, int(ae_exp * 0.5))
                                if new_exp < ae_exp:
                                    self._ae_pending = (sensor, new_exp, ae_gain)
                                    ae_exp = new_exp
                                    cam.exposure = new_exp
                                    print(
                                        f"{cam_key} clipped AE exp={ae_exp} "
                                        f"ff={ff:.2f}",
                                        flush=True,
                                    )
                        continue
                    if cam_key == "ir":
                        ow, oh = out_size(name)
                        expect = _frame_bytes(name)
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
                                self._ae_pending = ("ov7251", new_exp, new_gain)
                                ae_exp, ae_gain = new_exp, new_gain
                                cam.exposure, cam.gain = new_exp, new_gain
                                print(
                                    f"IR AE queued exp={ae_exp} gain={ae_gain} "
                                    f"p95={p95:.0f} sat={sat:.3f}",
                                    flush=True,
                                )
                        except Exception as e:
                            print(f"IR AE skip: {e}", flush=True)
                    elif (
                        cam_key in ("front", "back")
                        and ae_n >= 8
                        and now - ae_last_t >= (0.45 if ae_n < 90 else ae_interval)
                    ):
                        ae_last_t = now
                        try:
                            # Drive AE from already-unpacked midtones via BGR
                            # subsample — never re-unpack MIPI on the hot path.
                            sample = bgr[::8, ::8]
                            if sample.ndim == 3:
                                grey = cv2.cvtColor(sample, cv2.COLOR_BGR2GRAY)
                            else:
                                grey = sample
                            p50 = float(np.percentile(grey, 50.0))
                            p95 = float(np.percentile(grey, 95.0))
                            sat = float((grey > 230).mean())
                            mean = p50
                            if p95 < 40:
                                mean = min(mean, 35.0)
                            new_exp, new_gain = sc.continuous_ae_rgb(
                                sensor,
                                mean,
                                sat,
                                ae_exp,
                                ae_gain,
                                target=float(cam.target_mean),
                                p95=p95,
                            )
                            if new_exp != ae_exp or new_gain != ae_gain:
                                self._ae_pending = (sensor, new_exp, new_gain)
                                ae_exp, ae_gain = new_exp, new_gain
                                cam.exposure, cam.gain = new_exp, new_gain
                                print(
                                    f"{cam_key} AE queued exp={ae_exp} gain={ae_gain} "
                                    f"p50={p50:.0f} p95={p95:.0f} sat={sat:.3f}",
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

        def _ae_worker() -> None:
            while not self.stop.is_set():
                job = self._ae_pending
                self._ae_pending = None
                if job:
                    try:
                        sc.set_sensor_exposure(job[0], job[1], job[2])
                    except Exception as e:
                        print(f"AE apply skip: {e}", flush=True)
                self.stop.wait(0.4)

        self._threads = [
            threading.Thread(target=_reader, daemon=True, name=f"cap-{name}"),
            threading.Thread(target=_decode, daemon=True, name=f"dec-{name}"),
            threading.Thread(target=_ae_worker, daemon=True, name=f"ae-{name}"),
        ]
        for t in self._threads:
            t.start()
        if cam_key == "ir":
            self._start_ir_led()

            def _rear_led() -> None:
                time.sleep(0.6)
                if (
                    not self.stop.is_set()
                    and not self._ir_led_stop.is_set()
                    and family(self.name) == "ir"
                ):
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
        try:
            fds[name] = _open_loopback(DEV[name], ow, oh, fps, _loopback_fourcc(name))
            print(f"loopback {name} {DEV[name]} {ow}x{oh} @{fps}", flush=True)
        except OSError as e:
            # One busy node must not kill every camera and the login session.
            fds[name] = -1
            print(f"loopback {name} deferred ({e})", flush=True)

    cap = Capture()
    cap.name = "idle"
    cap.last_yuyv.update(load_hold_frames())
    last_switch = time.monotonic()
    last_front_rd = 0.0
    last_back_rd = 0.0
    last_ir_rd = 0.0
    last_seen_rd = {n: 0.0 for n in PROFILE_ORDER}
    stop = threading.Event()
    if not ok:
        cap._hold_only = True
        cap._backoff_until = time.monotonic() + DEAD_BACKOFF
        print(f"IPU unhealthy — hold-only, no STREAMON: {msg}", flush=True)

    tick = {"n": 0}
    cfr_fps = 30
    reader_since: dict[str, float] = {}
    had_reader: dict[str, bool] = {}

    def _reopen(name: str) -> None:
        # Reader may still hold exclusive_caps — WRONLY open then fails EBUSY.
        # Never storm reopen while apps are attached (guvcview SIGSEGV).
        try:
            if readers_of(DEV.get(name, "")):
                return
        except Exception:
            pass
        if time.monotonic() - last_seen_rd.get(name, 0.0) < 3.0:
            return
        old = fds.get(name, -1)
        if old >= 0:
            if _fd_has_capture(old) or old in _MMAP_OUT:
                return
            sink = _MMAP_OUT.pop(old, None)
            if sink is not None:
                sink.close()
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
            fds[name] = _open_loopback(dev, ow, oh, fps, _loopback_fourcc(name))
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
            # Never walk /proc here — background refresher owns the cache.
            rmap = _READERS_CACHE.get("map") or {}
            if not isinstance(rmap, dict):
                rmap = {}
            softmap = _READERS_CACHE.get("soft") or {}
            if not isinstance(softmap, dict):
                softmap = {}
            # PipeWire clients (Firefox, Kamoso) open the loopback from
            # wireplumber, which is not a hard reader. The node state is
            # what says they are actually capturing.
            pw_on = set(_PW_CACHE.get("running") or ())
            any_rd = any(
                bool(rmap.get(DEV.get(n, "")))
                or bool(softmap.get(DEV.get(n, "")))
                or n in pw_on
                for n in PROFILE_ORDER
            )
            # Idle keepalive: exclusive_caps needs occasional frames, not 30fps×7.
            if active == "idle" and not any_rd:
                period = 1.0
            for name in PROFILE_ORDER:
                fd = fds.get(name, -1)
                dev = DEV.get(name, "")
                has_rd = (
                    bool(rmap.get(dev))
                    or bool(softmap.get(dev))
                    or name in pw_on
                )
                if has_rd:
                    last_seen_rd[name] = time.monotonic()
                    # Do NOT v4l2-ctl --set-parm while a reader is attached.
                    # A second open during STREAMON rewrites timeperframe to 1/-1
                    # and the next buffer arrives as a foreign fourcc. Discord's
                    # capturer then logs "Failed to convert capture frame from
                    # type 8 to I420" and the renderer SIGSEGVs; the preview
                    # stays on the mirrored "Loading" overlay.
                if fd < 0:
                    # Never reopen while a reader holds exclusive_caps (guvcview S_FMT race).
                    if not has_rd:
                        try:
                            _reopen(name)
                        except OSError:
                            pass
                    continue
                sink = _MMAP_OUT.get(fd)
                if sink is not None:
                    # Discord's CaptureThread aborts (race_checker) if the fd is
                    # already readable while StartCapture is still on the stack.
                    # Keep the queue empty until that call has returned.
                    if not has_rd:
                        if had_reader.get(name):
                            sink.drain()
                            had_reader[name] = False
                        continue
                    if not had_reader.get(name):
                        sink.drain()
                        reader_since[name] = time.monotonic()
                        had_reader[name] = True
                    if time.monotonic() - reader_since.get(name, 0.0) < 0.40:
                        continue
                is_active = name == active
                # Only push frames to active profile + current readers.
                if not is_active and not has_rd:
                    if active == "idle" and not any_rd:
                        # Round-robin 1 device/sec — keeps CAPTURE without HQ CPU burn.
                        if PROFILE_ORDER[tick["n"] % len(PROFILE_ORDER)] != name:
                            continue
                    elif tick["n"] % 30 != 0:
                        continue
                if tick["n"] % 90 == 0 and fd not in _MMAP_OUT and not _fd_has_capture(fd):
                    if has_rd:
                        continue
                    print(f"loopback {name} lost CAPTURE — reopen", flush=True)
                    try:
                        _reopen(name)
                    except OSError:
                        pass
                    last_full[name] = time.monotonic()
                    continue
                if has_rd and name not in cap._real_frames:
                    # Flat placeholder (entropy 0) is what Discord froze on,
                    # then CaptureThread SIGSEGV'd. Wait for a real frame.
                    continue
                yuyv = cap.last_yuyv.get(name) or loading_yuyv(name)
                expect = _frame_bytes(name)
                if len(yuyv) != expect:
                    yuyv = loading_yuyv(name)
                n = _write_yuyv(fd, yuyv)
                if n == len(yuyv):
                    last_full[name] = time.monotonic()
                    cap.stats["out"] += 1
                elif n < 0:
                    if not has_rd:
                        try:
                            _reopen(name)
                        except OSError:
                            pass
                        last_full[name] = time.monotonic()
            time.sleep(max(0.0, period - (time.time() - t0)))

    threading.Thread(target=_cfr, daemon=True, name="cfr").start()
    start_readers_refresher(stop)

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
            # Single cached map — never N× /proc walks for LIVE logging.
            rmap = _READERS_CACHE.get("map") if isinstance(_READERS_CACHE.get("map"), dict) else {}
            rf = {p: sorted(rmap.get(DEV[p], ())) for p in profiles_of("front")}
            rb = {p: sorted(rmap.get(DEV[p], ())) for p in profiles_of("back")}
            ri = sorted(rmap.get(DEV["ir"], ()))
            extra = (
                f"n={dec} got={got} unique_fps={inst:.1f} drop={cap.stats['drop']} "
                f"out={cap.stats['out']} cam={cap.name} "
                f"ir_owner={owner} ir_led={led_mode} "
                f"rf={rf} rb={rb} "
                f"ri={ri} "
                f"pw={sorted(pw_running_cams())}"
            )
            print(f"LIVE {extra}", flush=True)
            write_status(cap.name, extra)
            ind = "idle"
            if family(cap.name) in ("front", "back"):
                if any(rmap.get(DEV.get(p, "")) for p in profiles_of(family(cap.name))):
                    ind = cap.name
            try:
                INDICATOR.parent.mkdir(parents=True, exist_ok=True)
                INDICATOR.write_text(ind + "\n")
            except OSError:
                pass
            t_log = now
            n_log = dec
        has_seed = any(cap.last_yuyv.get(n) for n in PROFILE_ORDER)
        pw = pw_running_cams()
        rmap = _READERS_CACHE.get("map") if isinstance(_READERS_CACHE.get("map"), dict) else {}
        if any(rmap.get(DEV[p]) or p in pw for p in profiles_of("front")):
            last_front_rd = now
        if any(rmap.get(DEV[p]) or p in pw for p in profiles_of("back")):
            last_back_rd = now
        if rmap.get(DEV["ir"]) or "ir" in pw:
            last_ir_rd = now
        if IR_ENABLED and (rmap.get(DEV["ir"]) or "ir" in pw or family(cap.name) == "ir"):
            enforce_ir_mutex()
        # Privacy LED only for real front/back STREAMON with hard readers.
        # Soft PW publish / idle Cheese must never leave the LED stuck on.
        if cap.name == "idle" or family(cap.name) not in ("front", "back"):
            privacy_leds_off()
        else:
            hard_rgb = any(
                bool(rmap.get(DEV.get(p, "")))
                for p in PROFILE_ORDER
                if p != "ir"
            )
            if not hard_rgb:
                privacy_leds_off()
        if cap._hold_only or now < cap._ir_halt_block_until:
            ir_dev = DEV.get("ir", "")
            ir_reader = bool(rmap.get(ir_dev)) or ("ir" in pw)
            # A sterile IR open used to latch for an hour. After login the
            # reader is gone and the flood LED stayed on.
            if not ir_reader and cap.stats.get("dec", 0) > 0:
                cap._hold_only = False
                cap._backoff_until = 0.0
            else:
                if not ir_reader:
                    cap._stop_ir_led()
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
                        cap._ir_halt_block_until = time.monotonic() + 15.0
                        cap._backoff_until = cap._ir_halt_block_until
                        last_switch = time.monotonic()
                        continue
                    cap.name = "idle"
                    cap._backoff_until = time.monotonic() + FAIL_BACKOFF
                    last_switch = time.monotonic()
                    continue
                if got > 0 and age > 2.0 and age < 2.5:
                    print(f"IR locked DQBUF got={got} (waiting non-0xFF decode)", flush=True)
            if (
                want == "idle"
                and family(cap.name) == "front"
                and cap.stats.get("dec", 0) == 0
                and now - last_switch < 3.0
            ):
                # Reader cache blips while media-ctl is still running and
                # decode has not published yet. STREAMOFF here leaves the
                # opener on zero buffers (black) until the next start.
                time.sleep(0.25)
                continue
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
                            cap._ir_halt_block_until = time.monotonic() + 15.0
                            cap._backoff_until = cap._ir_halt_block_until
                            cap._stop_ir_led()
                            print("IR hold-only (dec=0) — LED off, no idle storm", flush=True)
                        last_switch = time.monotonic()
                        # Even if STREAMOFF refused, RGB privacy LED must not stick.
                        privacy_leds_off()
                        continue
                    cap.name = "idle"
                privacy_leds_off()
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
                    # Match real ISYS-dead RuntimeError — NOT paths like isys-dead EACCES.
                    hard = (
                        "isys dead before" in err
                        or "unhealthy" in err
                        or "isys hang" in err
                    )
                    if hard:
                        cap._hold_only = True
                        cap._backoff_until = time.monotonic() + DEAD_BACKOFF
                        print(
                            f"ISYS dead — hold-only {DEAD_BACKOFF:.0f}s, no storm",
                            flush=True,
                        )
                    else:
                        # Soft failure (AF/format/perms/etc.) — retry soon, not 8s black hole
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
