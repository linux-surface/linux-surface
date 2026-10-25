#!/usr/bin/env python3
"""Surface Book 3 — libcamera raw → v4l2loopback (kernel 7.3).

Back: long-lived cam on /dev/shm. BayerRG, NO flip (libcamera already upright).
Profiles match 6.19.8-3 / 7.2.12 (cap/out/fps). Fast is a 16:9 crop of that mode.

IR: power ov7251 first, then ir-led-on (LED needs sensor awake on i2c-3).
Front: timing mode 2 (calc settle + Windows TERMEN). Capture 2592x1944 SBGGR10; publish canon sizes. LED only with a client.
"""
from __future__ import annotations

import argparse
import os
import select
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

import cv2
import numpy as np

# label, video_nr, cap_w, cap_h, out_w, out_h, fps
# Must match contrib PRE-COMMIT-CHECKLIST / surface_webcamd.PROFILES.
BACK_PROFILES = [
    ("Surface-Back-Standard", 58, 1632, 1224, 1632, 1224, 30),
    ("Surface-Back-HQ", 59, 3264, 2448, 1920, 1440, 15),
    ("Surface-Back-Fast", 60, 800, 600, 800, 450, 60),
]

FRONT_PROFILES = [
    ("Surface-Front-Standard", 55, 1296, 972, 1296, 972, 30),
    ("Surface-Front-HQ", 56, 2592, 1944, 1920, 1440, 15),
    ("Surface-Front-Fast", 57, 1296, 972, 1296, 728, 60),
]

_WB_DEFAULT = np.array([1.16, 0.95, 1.16], dtype=np.float32)
_OV7251_PM = "/sys/bus/i2c/devices/i2c-INT347E:00/power"


def find_loopback(label: str) -> str | None:
    for p in sorted(Path("/sys/class/video4linux").glob("video*")):
        try:
            if (p / "name").read_text().strip() == label:
                return f"/dev/{p.name}"
        except OSError:
            continue
    return None


def find_subdev(substr: str) -> str | None:
    for p in sorted(Path("/sys/class/video4linux").glob("v4l-subdev*")):
        try:
            if substr in (p / "name").read_text():
                return f"/dev/{p.name}"
        except OSError:
            continue
    return None


def find_cam(id_substr: str) -> int | None:
    try:
        out = subprocess.check_output(
            ["cam", "-l"], stderr=subprocess.STDOUT, text=True, timeout=25
        )
    except Exception:
        return None
    for line in out.splitlines():
        s = line.strip()
        if not s or not s[0].isdigit() or ": " not in s:
            continue
        a, b = s.split(":", 1)
        if a.isdigit() and id_substr in b:
            return int(a)
    return None


def v4l_set(dev: str, ctrls: str) -> None:
    subprocess.run(
        ["v4l2-ctl", "-d", dev, "-c", ctrls], capture_output=True, timeout=2
    )


def heal_loopback_fps(dev: str, fps: int) -> None:
    """Browsers send timeperframe 1/0. v4l2loopback stores that as one frame
    per 2^32 seconds, and sustain_framerate then never wakes the reader.
    """
    p = Path("/sys/devices/virtual/video4linux") / Path(dev).name / "format"
    try:
        txt = p.read_text()
    except OSError:
        return
    if "4294967295" not in txt and not txt.strip().endswith("@0"):
        return
    try:
        p.write_text('@%d/1' % int(fps) + chr(10))
        print(f"loopback fps heal {dev} -> {fps}", flush=True)
    except OSError as e:
        print(f"loopback fps heal {dev}: {e}", flush=True)


def open_loopback(dev: str, w: int, h: int, fps: int = 30) -> int:
    subprocess.run(
        ["v4l2-ctl", "-d", dev, "-c", "keep_format=0"], capture_output=True, timeout=2
    )
    time.sleep(0.03)
    subprocess.run(
        [
            "v4l2-ctl",
            "-d",
            dev,
            "--set-fmt-video-out",
            f"width={w},height={h},pixelformat=YUYV",
        ],
        capture_output=True,
        timeout=2,
    )
    subprocess.run(
        ["v4l2-ctl", "-d", dev, f"--set-parm={fps}"], capture_output=True, timeout=2
    )
    subprocess.run(
        ["v4l2-ctl", "-d", dev, "-c", "keep_format=1,sustain_framerate=1"],
        capture_output=True,
        timeout=2,
    )
    fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
    # Hold the OUTPUT stream token so exclusive_caps announces CAPTURE.
    # Without a queued frame the node is output-only and browsers skip it.
    write_yuyv(fd, yuyv_color(w, h, 0, 128, 128))
    try:
        fmt = Path("/sys/devices/virtual/video4linux") / Path(dev).name / "format"
        fmt.write_text('@%d/1' % int(fps) + chr(10))
    except OSError as e:
        print(f"loopback fps {dev}: {e}", flush=True)
    return fd



def yuyv_color(w: int, h: int, y: int = 0, u: int = 128, v: int = 128) -> bytes:
    """Solid YUYV. y=0 is black — used as loading / switch placeholder."""
    return bytes([y & 0xFF, u & 0xFF, y & 0xFF, v & 0xFF]) * max(1, (w * h) // 2)


def wipe_loopbacks(
    fds: dict[int, int],
    sizes: dict[int, tuple[int, int]],
    nrs: set[int] | None = None,
) -> None:
    """Overwrite loopbacks with black so sustain_framerate cannot show a stale frame."""
    targets = nrs if nrs is not None else set(fds.keys())
    for nr in targets:
        fd = fds.get(nr)
        if fd is None:
            continue
        w, h = sizes.get(nr, (640, 480))
        try:
            write_yuyv(fd, yuyv_color(w, h, 0, 128, 128))
        except OSError:
            pass


def write_yuyv(fd: int, data: bytes) -> None:
    """Write one full YUYV frame without stalling the CSI thread.

    A blocking write while Discord holds the loopback used to freeze the
    daemon: no RAW consume, black preview, privacy LED stuck on.
    """
    view = memoryview(data)
    off = 0
    deadline = time.monotonic() + 0.15
    try:
        os.set_blocking(fd, False)
    except OSError:
        pass
    try:
        while off < len(data):
            if time.monotonic() > deadline:
                return
            try:
                n = os.write(fd, view[off:])
            except BlockingIOError:
                # Wait briefly for the reader to drain.
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return
                try:
                    select.select([], [fd], [], min(0.05, remaining))
                except (ValueError, OSError):
                    return
                continue
            except OSError:
                return
            if n is None or n <= 0:
                return
            off += n
    finally:
        try:
            os.set_blocking(fd, False)
        except OSError:
            pass


def bgr_to_yuyv(bgr: np.ndarray) -> bytes:
    yuv = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV)
    y, u, v = cv2.split(yuv)
    h, w = y.shape
    out = np.empty((h, w * 2), dtype=np.uint8)
    out[:, 0::4] = y[:, 0::2]
    out[:, 1::4] = u[:, 0::2]
    out[:, 2::4] = y[:, 1::2]
    out[:, 3::4] = v[:, 0::2]
    return out.tobytes()


def fit_profile(bgr: np.ndarray, ow: int, oh: int) -> np.ndarray:
    """Resize without stretch. Fast 800x450 = 16:9 center-crop then scale."""
    h, w = bgr.shape[:2]
    if w == ow and h == oh:
        return bgr
    # Target aspect: crop source to match, then scale
    src_ar = w / h
    dst_ar = ow / oh
    if abs(src_ar - dst_ar) > 0.02:
        if src_ar > dst_ar:
            nw = int(h * dst_ar) & ~1
            x0 = ((w - nw) // 2) & ~1
            bgr = bgr[:, x0 : x0 + nw]
        else:
            nh = int(w / dst_ar) & ~1
            y0 = ((h - nh) // 2) & ~1
            bgr = bgr[y0 : y0 + nh, :]
    if bgr.shape[1] != ow or bgr.shape[0] != oh:
        bgr = cv2.resize(bgr, (ow, oh), interpolation=cv2.INTER_AREA)
    return bgr


_wb_scales = _WB_DEFAULT.copy()
_wb_n = 0


def demosaic_back(buf: bytes) -> np.ndarray | None:
    """BayerRG, upright (no flip — libcamera path). Skip magenta/unlock frames."""
    global _wb_scales, _wb_n
    arr = np.frombuffer(buf, dtype=np.uint16)
    n = arr.size
    if n < _back_wh[0] * _back_wh[1]:
        return None
    img = arr[: _back_wh[0] * _back_wh[1]].reshape(_back_wh[1], _back_wh[0])
    sample = img[::16, ::16]
    p1 = float(sample.min())
    p99 = float(np.percentile(sample, 99))
    if p99 - p1 < 15 and p99 < 50:
        return None
    sat = float((sample >= 1000).mean())
    # Do not drop a bright scene. A lamp is a picture; only a dead frame
    # (almost no range) is rejected, and the caller must not freeze on it.

    lo = min(max(p1, 0.0), 40.0)
    hi = max(p99, lo + 80.0)
    grey = cv2.convertScaleAbs(
        img, alpha=255.0 / max(hi - lo, 1.0), beta=-lo * 255.0 / max(hi - lo, 1.0)
    )
    bgr = cv2.cvtColor(grey, cv2.COLOR_BayerRG2BGR)
    # libcamera path: horizontal mirror only (flip_v was upside-down)
    bgr = cv2.flip(bgr, 1)

    _wb_n += 1
    if _wb_n <= 4 or _wb_n % 60 == 0:
        s = bgr[::8, ::8].astype(np.float32)
        flat = s.reshape(-1, 3)
        lum = flat.mean(axis=1)
        mid = flat[(lum > 45) & (lum < 190)]
        if len(mid) < 50:
            mid = flat
        means = np.maximum(mid.mean(axis=0), 1.0)
        gw = np.clip(float(means.mean()) / means, 0.75, 1.45).astype(np.float32)
        scales = (0.35 * gw + 0.65 * _WB_DEFAULT).astype(np.float32)
        scales[0] = float(np.clip(float(scales[0]), 1.08, 1.38))
        scales[1] = float(np.clip(float(scales[1]), 0.80, 0.96))
        scales[2] = float(np.clip(float(scales[2]), 1.08, 1.32))
        _wb_scales = scales
        if _wb_n <= 4 or _wb_n % 120 == 0:
            print(f"WB scales={np.round(_wb_scales, 3).tolist()}", flush=True)

    bgr = cv2.transform(bgr, np.diag(_wb_scales.astype(np.float64)))
    bgr = cv2.transform(bgr, np.diag([1.03, 0.90, 1.05]).astype(np.float64))

    # Magenta guard: reject frame if midtones are magenta-dominant
    s = bgr[::16, ::16].astype(np.float32)
    lum = s.mean(axis=2)
    mid = s[(lum > 50) & (lum < 200)]
    if len(mid) >= 40:
        mb, mg, mr = mid.mean(axis=0)
        mag = float((mr + mb) * 0.5 - mg)
        if mag > 18.0:
            return None

    mean = float(cv2.cvtColor(bgr[::8, ::8], cv2.COLOR_BGR2GRAY).mean())
    if mean < 80.0:
        bgr = cv2.convertScaleAbs(
            bgr, alpha=1.10 + (80.0 - mean) / 80.0 * 0.40, beta=10.0
        )
    return bgr


class SoftAE:
    def __init__(self, subdev: str):
        self.subdev = subdev
        self.exp = 900
        self.gain = 384
        self._stop = False
        self._lock = threading.Lock()
        self._last = 0.0
        self.apply()

    def apply(self) -> None:
        g = int(np.clip(round(self.gain / 128) * 128, 128, 1408))
        self.gain = g
        e = int(np.clip(self.exp, 80, 2000))
        self.exp = e
        v4l_set(self.subdev, f"exposure={e},analogue_gain={g}")

    def _hammer(self) -> None:
        while not self._stop:
            with self._lock:
                self.apply()
            time.sleep(0.12)

    def stop(self) -> None:
        self._stop = True

    def update_raw(self, img: np.ndarray) -> None:
        now = time.monotonic()
        if now - self._last < 1.5:
            return
        self._last = now
        s = img[::8, ::8]
        mean = float(s.mean())
        p95 = float(np.percentile(s, 95))
        p99 = float(np.percentile(s, 99))
        sat = float((s >= 1000).mean())
        if sat > 0.015 or p99 >= 1000:
            f = 0.75
        elif p95 > 850:
            f = 0.86
        elif p95 > 750 and mean > 220:
            f = 0.92
        elif mean > 350:
            f = 0.96
        elif mean < 90:
            f = 1.12
        elif mean < 140:
            f = 1.06
        elif mean < 200:
            f = 1.02
        else:
            return
        with self._lock:
            if f < 1.0:
                if self.gain > 256:
                    self.gain = int(np.clip(self.gain * f, 128, 1408))
                self.exp = int(np.clip(self.exp * f, 80, 2000))
            else:
                self.exp = int(np.clip(self.exp * f, 80, 2000))
                if self.exp > 1600:
                    self.gain = int(np.clip(self.gain * min(f, 1.06), 128, 1024))
            self.apply()


def _pm_write(rel: str, value: str) -> None:
    path = f"{_OV7251_PM}/{rel}"
    try:
        with open(path, "w") as f:
            f.write(f"{value}\n")
    except OSError:
        pass


def ov7251_power(on: bool) -> None:
    if on:
        _pm_write("control", "on")
        t0 = time.monotonic()
        while time.monotonic() - t0 < 2.0:
            try:
                st = Path(f"{_OV7251_PM}/runtime_status").read_text().strip()
            except OSError:
                st = ""
            if st == "active":
                break
            time.sleep(0.05)
        time.sleep(0.25)
    else:
        _pm_write("control", "auto")


def ir_led(on: bool, span: str = "0x02f4") -> bool:
    """Must call ov7251_power(True) first — otherwise i2c Remote I/O error."""
    bin_path = "/usr/local/bin/ir-led-on"
    if not os.path.isfile(bin_path):
        return False
    cmd = [bin_path, "on", span] if on else [bin_path, "off"]
    # Prefer direct root; fall back to sudo -n
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=3)
    if r.returncode != 0:
        r = subprocess.run(
            ["sudo", "-n", *cmd], capture_output=True, text=True, timeout=3
        )
    out = (r.stdout or "") + (r.stderr or "")
    ok = r.returncode == 0 and "Remote I/O error" not in out and "chip id 0000" not in out
    print(f"ir-led {'on' if on else 'off'}: rc={r.returncode} {out.strip()[:120]}", flush=True)
    return ok


def run_back(stop: threading.Event) -> None:
    idx = find_cam("CAMR")
    sub = find_subdev("ov8865")
    lens = find_subdev("dw9719")
    if idx is None or sub is None:
        print("CAMR/ov8865 missing", flush=True)
        return
    if lens:
        v4l_set(lens, "focus_absolute=350")

    fds: dict[int, int] = {}
    for name, nr, _cw, _ch, ow, oh, fps in BACK_PROFILES:
        loop = find_loopback(name) or f"/dev/video{nr}"
        if not Path(loop).exists():
            print(f"missing {loop}", flush=True)
            continue
        try:
            fds[nr] = open_loopback(loop, ow, oh, fps=fps)
            print(f"{name}: {loop} {ow}x{oh} @{fps}", flush=True)
        except OSError as e:
            print(f"{name}: {e}", flush=True)
    if 58 not in fds:
        print("Back-Standard required — exit", flush=True)
        return

    v4l_set(sub, "red_balance=1050,blue_balance=980")
    ae = SoftAE(sub)
    shm = Path("/dev/shm/surface-lc-back")
    if shm.exists():
        shutil.rmtree(shm, ignore_errors=True)
    shm.mkdir(parents=True, exist_ok=True)
    pattern = str(shm / "f-#.raw")
    frames = 0
    skipped = 0
    last_good: np.ndarray | None = None
    print(f"CAMR live cam={idx} shm={shm} (LED off until a client opens the loopback)", flush=True)
    loop_devs = []
    loop_by_nr: dict[int, str] = {}
    for name, nr, _cw, _ch, _ow, _oh, _fps in BACK_PROFILES:
        if nr in fds:
            loop_by_nr[nr] = find_loopback(name) or f"/dev/video{nr}"
    loop_devs = list(loop_by_nr.values())
    last_clients = -1
    next_check = 0.0

    def clients_now() -> int:
        n = 0
        for dev in loop_devs:
            try:
                n += front_clients(dev)
            except OSError:
                pass
        return n

    while not stop.is_set():
        now = time.monotonic()
        if now >= next_check or last_clients < 0:
            next_check = now + 0.1
            clients = clients_now()
            if clients != last_clients:
                print(f"CAMR clients={clients}", flush=True)
                last_clients = clients
            for _name, nr, _cw, _ch, _ow, _oh, fps in BACK_PROFILES:
                dev = loop_by_nr.get(nr)
                if dev:
                    heal_loopback_fps(dev, fps)
        else:
            clients = last_clients
        if clients <= 0:
            time.sleep(0.05)
            continue
        global _back_wh
        open_nrs = {nr for nr, dev in loop_by_nr.items() if front_clients(dev) > 0}
        if 59 in open_nrs:
            cap_w, cap_h, vblank = 3264, 2448, 1200
        elif open_nrs == {60}:
            cap_w, cap_h, vblank = 800, 600, 320
        else:
            cap_w, cap_h, vblank = 1632, 1224, 1200
        _back_wh = (cap_w, cap_h)
        v4l_set(sub, f"vertical_blanking={vblank}")
        proc = subprocess.Popen(
            [
                "cam",
                "-c",
                str(idx),
                "--stream",
                f"role=raw,width={cap_w},height={cap_h}",
                "--capture=0",
                f"--file={pattern}",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        print(f"CAMR: cam pid={proc.pid}", flush=True)
        wipe_loopbacks(fds, {nr: (ow, oh) for _n, nr, _cw, _ch, ow, oh, _fps in BACK_PROFILES})
        seen: set[str] = set()
        idle = 0
        while not stop.is_set() and proc.poll() is None:
            now = time.monotonic()
            if now >= next_check:
                next_check = now + 0.1
                clients = clients_now()
                if clients != last_clients:
                    print(f"CAMR clients={clients}", flush=True)
                    last_clients = clients
                if clients <= 0 or any_external_clients(FRONT_LOOPBACKS + IR_LOOPBACKS):
                    print("CAMR no client → STREAMOFF (LED off)", flush=True)
                    wipe_loopbacks(fds, {nr: (ow, oh) for _n, nr, _cw, _ch, ow, oh, _fps in BACK_PROFILES})
                    _stop_proc(proc)
                    break
                open_nrs = {nr for nr, dev in loop_by_nr.items() if front_clients(dev) > 0}
            files = sorted(shm.glob("*.raw"))
            need_b = cap_w * cap_h * 2
            now_wall = time.time()
            ready: list[Path] = []
            for cand in files:
                if cand.name in seen:
                    continue
                try:
                    st = cand.stat()
                except OSError:
                    continue
                if st.st_size >= need_b and (now_wall - st.st_mtime) >= 0.001:
                    ready.append(cand)
            if not ready:
                idle += 1
                time.sleep(0.001 if idle < 80 else 0.01)
                continue
            idle = 0
            # Only drop when the queue is already a backlog, and keep the newest.
            if len(ready) > 4:
                for old in ready[:-4]:
                    try:
                        old.unlink()
                    except OSError:
                        pass
                    seen.add(old.name)
                    skipped += 1
                ready = ready[-4:]
            if len(seen) > 400:
                seen.clear()
            for path in ready:
                seen.add(path.name)
                try:
                    data = path.read_bytes()
                    path.unlink(missing_ok=True)
                except OSError:
                    continue
                arr = np.frombuffer(data, dtype=np.uint16)
                need = cap_w * cap_h
                if arr.size >= need:
                    ae.update_raw(arr[:need].reshape(cap_h, cap_w))
                bgr = demosaic_back(data)
                if bgr is None:
                    skipped += 1
                    continue
                last_good = bgr
                for name, nr, _cw, _ch, ow, oh, _fps in BACK_PROFILES:
                    if nr not in open_nrs:
                        continue
                    fd = fds.get(nr)
                    if fd is None:
                        continue
                    if nr == 59 and frames % 2:
                        continue
                    write_yuyv(fd, bgr_to_yuyv(fit_profile(bgr, ow, oh)))
                frames += 1
                if frames == 1 or frames % 150 == 0:
                    print(
                        f"CAMR: n={frames} skip={skipped} exp={ae.exp} "
                        f"gain={ae.gain} mean={float(bgr.mean()):.1f}",
                        flush=True,
                    )
        _stop_proc(proc)
        print(f"CAMR: cam exit={proc.returncode}", flush=True)
        for p in shm.glob("*"):
            p.unlink(missing_ok=True)
        if stop.is_set():
            break
        time.sleep(0.05)

    ae.stop()
    for fd in fds.values():
        for fdw in fds.values():
            try:
                os.close(fdw)
            except OSError:
                pass
    shutil.rmtree(shm, ignore_errors=True)
    print(f"CAMR stopped n={frames}", flush=True)



def _dev_pids(dev: str) -> set[int]:
    import stat

    rdev = os.stat(dev).st_rdev
    pids: set[int] = set()
    for ent in os.listdir("/proc"):
        if not ent.isdigit():
            continue
        fdd = f"/proc/{ent}/fd"
        try:
            names = os.listdir(fdd)
        except OSError:
            continue
        for fd in names:
            try:
                st = os.stat(f"{fdd}/{fd}")
            except OSError:
                continue
            if stat.S_ISCHR(st.st_mode) and st.st_rdev == rdev:
                pids.add(int(ent))
                break
    return pids


BACK_LOOPBACKS = ("/dev/video58", "/dev/video59", "/dev/video60")
FRONT_LOOPBACKS = ("/dev/video55", "/dev/video56", "/dev/video57")
IR_LOOPBACKS = ("/dev/video66",)
_client_cache: dict[str, int] = {}
_client_cache_t = 0.0
_daemon_cache: set[int] = set()
_daemon_cache_t = 0.0


def _daemon_pids() -> set[int]:
    """Camera daemons hold their own loopbacks. That is not a client."""
    global _daemon_cache, _daemon_cache_t
    now = time.monotonic()
    if _daemon_cache and now - _daemon_cache_t < 2.0:
        return _daemon_cache
    found: set[int] = set()
    for ent in os.listdir("/proc"):
        if not ent.isdigit():
            continue
        try:
            cmd = Path(f"/proc/{ent}/cmdline").read_bytes()
        except OSError:
            continue
        if b"surface_lc_webcamd" in cmd:
            found.add(int(ent))
    _daemon_cache = found
    _daemon_cache_t = now
    return found


def _refresh_clients() -> None:
    """One /proc pass for every loopback. Six separate scans capped fps at ~5."""
    global _client_cache, _client_cache_t
    now = time.monotonic()
    if _client_cache and now - _client_cache_t < 0.2:
        return
    wanted = {dev: 0 for dev in FRONT_LOOPBACKS + BACK_LOOPBACKS + IR_LOOPBACKS}
    daemons = _daemon_pids()
    for ent in os.listdir("/proc"):
        if not ent.isdigit() or int(ent) in daemons:
            continue
        fdd = f"/proc/{ent}/fd"
        try:
            fds = os.listdir(fdd)
        except OSError:
            continue
        for fd in fds:
            try:
                link = os.readlink(f"{fdd}/{fd}")
            except OSError:
                continue
            if link in wanted:
                wanted[link] += 1
    _client_cache = wanted
    _client_cache_t = now


def front_clients(dev: str) -> int:
    """Other processes holding the loopback. The daemons' own fds do not count."""
    _refresh_clients()
    return _client_cache.get(dev, 0)


def any_external_clients(devs: tuple[str, ...]) -> bool:
    _refresh_clients()
    return any(_client_cache.get(dev, 0) > 0 for dev in devs)


_front_wh = (1296, 972)


def demosaic_front(buf: bytes) -> np.ndarray | None:
    """SBGGR10 2x2 bin. ov5693 top-left is blue; RGGB made skin blue.

    2592x1944 becomes 1296x972 with no OpenCV demosaic. Dead frames are dropped.
    """
    arr = np.frombuffer(buf, dtype=np.uint16)
    w, h = _front_wh
    need = w * h
    if arr.size < need:
        return None
    full = arr[:need].reshape(h, w)
    if h < 4 or w < 4:
        return None
    b = full[0::2, 0::2]
    g = (full[0::2, 1::2].astype(np.uint16, copy=False) >> 1) + (
        full[1::2, 0::2].astype(np.uint16, copy=False) >> 1
    )
    r = full[1::2, 1::2]
    # exp ~150 sits near raw 100-250. Fixed scale, no per-frame percentile.
    bgr = cv2.merge(
        (
            cv2.convertScaleAbs(b, alpha=1.7, beta=4),
            cv2.convertScaleAbs(g, alpha=1.20, beta=4),
            cv2.convertScaleAbs(r, alpha=1.7, beta=4),
        )
    )
    return cv2.flip(bgr, 1)


class SoftAEFront:
    """ov5693 AE, same bounds as 6.19 (exp 200–1030, gain 8–120).

    Exposure and gain are written through v4l. ov5693 latches them at the
    next start of frame (group hold 0x3208), so the live frame is not torn.
    """

    def __init__(self, subdev: str):
        self.subdev = subdev
        self.exp = 560
        self.gain = 24
        self._last = 0.0
        self._n = 0
        self._i2c = _ov5693_i2c(subdev)
        self._hold_ok = False
        self.apply()

    def apply(self) -> None:
        self.exp = int(np.clip(self.exp, 200, 1030))
        self.gain = int(np.clip(round(self.gain / 8) * 8, 8, 120))
        v4l_set(self.subdev, f"exposure={self.exp},analogue_gain={self.gain}")

    def stop(self) -> None:
        pass

    def note_unlock(self) -> None:
        return

    def update_bgr(self, bgr: np.ndarray) -> None:
        self._n += 1
        now = time.monotonic()
        interval = 0.45
        if self._n < 8 or now - self._last < interval:
            return
        self._last = now
        grey = cv2.cvtColor(bgr[::8, ::8], cv2.COLOR_BGR2GRAY)
        p50 = float(np.percentile(grey, 50))
        p95 = float(np.percentile(grey, 95))
        sat = float((grey > 230).mean())
        # A CSI insert is a full-width white band. A lamp is not.
        # Do not move exposure because of that one frame.
        row_sat = float((grey > 230).mean(axis=1).max())
        # Fully clipped / CSI unlock frames are not a bright room.
        if p50 > 240 or sat > 0.50 or (row_sat > 0.85 and sat < 0.55):
            return
        new_exp, new_gain = _front_ae_step(p50, p95, sat, self.exp, self.gain)
        if new_exp == self.exp and new_gain == self.gain:
            return
        self.exp, self.gain = new_exp, new_gain
        self.apply()
        print(
            f"CAMF AE exp={self.exp} gain={self.gain} p50={p50:.0f} p95={p95:.0f} sat={sat:.3f}",
            flush=True,
        )


def _ov5693_i2c(subdev: str) -> tuple[int, int] | None:
    """ACPI node is i2c-INT33BE:00 on bus i2c-N. The sensor answers at 0x36."""
    try:
        dev = Path(f"/sys/class/video4linux/{Path(subdev).name}/device").resolve()
        bus_name = dev.parent.name
        if not bus_name.startswith("i2c-"):
            return None
        return int(bus_name.split("-", 1)[1]), 0x36
    except (OSError, ValueError):
        return None


def _ov5693_group(i2c: tuple[int, int] | None, val: int) -> bool:
    """OV5693 group-hold at 0x3208. 0x00 start, 0x10 end, 0xA0 launch at SOF."""
    if not i2c:
        return False
    bus, addr = i2c
    proc = subprocess.run(
        [
            "i2ctransfer", "-f", "-y", str(bus), f"w3@0x{addr:02x}",
            "0x32", "0x08", f"0x{val & 0xff:02x}",
        ],
        capture_output=True,
        timeout=1,
    )
    return proc.returncode == 0


def _front_ae_step(
    mean: float, p95: float, sat: float, exp: int, gain: int, target: float = 115.0
) -> tuple[int, int]:
    """One step. Same shape as 6.19 continuous_ae_rgb for ov5693."""
    exp_lo, exp_hi, gain_lo, gain_hi, gstep = 200, 1030, 8, 120, 8
    flat_clip = (p95 - mean) < 12.0 and mean > 150.0
    if sat > 0.20 or flat_clip:
        new_exp = max(exp_lo, int(exp * 0.5))
        new_gain = gain
        if new_exp <= exp_lo + 5:
            new_gain = max(gain_lo, gain - gstep)
    elif sat > 0.015 or p95 > 200 or mean > target + 22:
        new_exp = max(exp_lo, exp - max(40, exp // 5))
        new_gain = gain
        if new_exp <= exp_lo + 5 and (mean > target + 12 or p95 > 195):
            new_gain = max(gain_lo, gain - gstep)
    elif mean < 40.0:
        new_exp = min(exp_hi, exp + max(48, exp // 6))
        new_gain = gain
        if new_exp >= exp_hi - 5 and mean < 35.0:
            new_gain = min(gain_hi, gain + gstep * 2)
    elif mean < target - 18:
        new_exp = min(exp_hi, exp + max(16, exp // 12))
        new_gain = gain
        if new_exp >= exp_hi - 5 and mean < target - 14:
            new_gain = min(gain_hi, gain + gstep)
    else:
        return exp, gain
    return int(new_exp), int(new_gain)



_CSI_COOL_UNTIL = 0.0

def _camf_log(msg: str) -> None:
    print(msg, flush=True)
    try:
        with open("/tmp/camf-daemon.log", "a") as f:
            f.write(msg + "\n")
    except OSError:
        pass

_PM_QOS_FD: int | None = None


def _note_csi_stop() -> None:
    """Front DPHY needs a cool-down after STREAMOFF before the next lock."""
    global _CSI_COOL_UNTIL
    _CSI_COOL_UNTIL = time.monotonic() + 3.5


def _wait_csi_cool() -> None:
    left = _CSI_COOL_UNTIL - time.monotonic()
    if left > 0:
        time.sleep(left)


def _pm_qos_hold(enable: bool) -> None:
    """Hold CPU latency at 100 us while CSI streams (loafecho #1353 / C2 stall)."""
    global _PM_QOS_FD
    if enable:
        if _PM_QOS_FD is not None:
            return
        try:
            fd = os.open("/dev/cpu_dma_latency", os.O_WRONLY)
            os.write(fd, (100).to_bytes(4, "little"))
            _PM_QOS_FD = fd
            print("CAMF pm_qos hold 100us", flush=True)
        except OSError as e:
            print(f"CAMF pm_qos open failed: {e}", flush=True)
        return
    if _PM_QOS_FD is None:
        return
    try:
        os.close(_PM_QOS_FD)
    except OSError:
        pass
    _PM_QOS_FD = None


def _stop_proc(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=1.0)
    except Exception:
        proc.kill()
        try:
            proc.wait(timeout=0.5)
        except Exception:
            pass



# Mode 2 = calculated settle + Windows TERMEN (SB3 port-2). Mode 0 with
# fixed 1400/880 is pure-white/intermittent here; mode 2 locked 10/10 in probe.
# Do not thrash settle pairs — that dirties PHY.
_FRONT_SETTLE = (1000, 1000)  # unused in mode 2; kept for sysfs visibility
_FRONT_TIMING = 2


def _pin_front_settle() -> tuple[int, int]:
    clk, data = _FRONT_SETTLE
    for path, val in (
        ("/sys/module/intel_ipu6_isys/parameters/sb3_front_timing", str(_FRONT_TIMING)),
        ("/sys/module/intel_ipu6_isys/parameters/sb3_front_clk_settle", str(clk)),
        ("/sys/module/intel_ipu6_isys/parameters/sb3_front_data_settle", str(data)),
    ):
        try:
            Path(path).write_text(val + "\n")
        except OSError as e:
            print(f"front param {path}: {e}", flush=True)
    return clk, data


def run_front(stop: threading.Event) -> None:
    """Full FOV. STREAMON only while some other process has the loopback open."""
    idx = find_cam("CAMF")
    sub = find_subdev("ov5693")
    if idx is None or sub is None:
        print("CAMF/ov5693 missing", flush=True)
        return
    _pin_front_settle()

    v4l_set(sub, "horizontal_flip=0,vertical_flip=0")
    fds: dict[int, int] = {}
    loop_by_nr: dict[int, str] = {}
    for name, nr, _cw, _ch, ow, oh, fps in FRONT_PROFILES:
        loop = find_loopback(name) or f"/dev/video{nr}"
        if not Path(loop).exists():
            print(f"missing {loop}", flush=True)
            continue
        fds[nr] = open_loopback(loop, ow, oh, fps=fps)
        loop_by_nr[nr] = loop
        print(f"{name}: {loop} {ow}x{oh} @{fps}", flush=True)
    if 55 not in fds:
        print("Front-Standard required", flush=True)
        return
    loop = loop_by_nr[55]

    ae = SoftAEFront(sub)
    shm = Path("/dev/shm/surface-lc-front")
    shm.mkdir(parents=True, exist_ok=True)
    pattern = str(shm / "f-#.raw")
    frames = 0
    t_fps = time.monotonic()
    n_fps = 0
    n_white = 0
    n_held = 0
    start_retries = 0
    raw_seen = 0
    proc: subprocess.Popen | None = None
    seen: set[str] = set()
    last_clients = -1
    next_client_check = 0.0
    last_client_mono = 0.0
    last_good = 0.0
    stream_t0 = 0.0
    stable = 0
    consecutive = 0
    active_nrs: set[int] = {55}
    out_of = {nr: (ow, oh) for _n, nr, _cw, _ch, ow, oh, _fps in FRONT_PROFILES}
    last_yuyv: dict[int, bytes] = {}
    print(f"CAMF ready cam={idx} LED off until a client opens {loop}", flush=True)

    try:
        while not stop.is_set():
            now = time.monotonic()
            if now >= next_client_check or proc is None:
                next_client_check = now + 0.1
                active_nrs = {nr for nr, dev in loop_by_nr.items() if front_clients(dev) > 0}
                clients = len(active_nrs)
                if clients:
                    last_client_mono = now
                if clients != last_clients:
                    print(f"CAMF clients={clients} nrs={sorted(active_nrs)}", flush=True)
                    # Black loading only when a client appears and we have no
                    # live picture yet. Never wipe a locked stream (sustain
                    # would then repeat black → "black inserts").
                    if clients > 0 and last_clients <= 0 and not last_yuyv:
                        wipe_loopbacks(fds, out_of)
                    last_clients = clients
                for _name, nr, _cw, _ch, _ow, _oh, fps in FRONT_PROFILES:
                    dev = loop_by_nr.get(nr)
                    if dev:
                        heal_loopback_fps(dev, fps)
            else:
                clients = last_clients
            if clients <= 0:
                # A back client must get the ISYS immediately. Same-side
                # profile changes only need a short hold so the PHY stays up.
                other_wants = any_external_clients(BACK_LOOPBACKS + IR_LOOPBACKS)
                # No post-close hold — privacy LED must go off when Discord closes.
                hold = 0.0
                if proc is not None and (now - last_client_mono) < hold:
                    active_nrs = set()
                else:
                    if proc is not None:
                        print("CAMF no client → STREAMOFF (LED off)", flush=True)
                        _stop_proc(proc)
                        proc = None
                        seen.clear()
                        stable = 0
                        consecutive = 0
                        frames = 0
                        n_white = 0
                        n_held = 0
                        last_yuyv.clear()
                        start_retries = 0
                        raw_seen = 0
                        wipe_loopbacks(fds, out_of)
                        _note_csi_stop()
                        _pm_qos_hold(False)
                    time.sleep(0.2)
                    continue
            if proc is None or proc.poll() is not None:
                if proc is not None and proc.poll() is not None:
                    print(f"CAMF cam exit rc={proc.returncode}", flush=True)
                    proc = None
                    time.sleep(0.6)
                for old in list(shm.glob("*.raw")):
                    try:
                        old.unlink()
                    except OSError:
                        pass
                seen.clear()
                global _front_wh
                cap_w, cap_h = 2592, 1944
                _front_wh = (cap_w, cap_h)
                clk, data = _pin_front_settle()
                ae.apply()
                _pm_qos_hold(True)
                _wait_csi_cool()
                print(
                    f"CAMF start timing={_FRONT_TIMING} settle {clk}/{data} (single STREAMON)",
                    flush=True,
                )
                cam_log = open("/tmp/camf-cam.log", "ab", buffering=0)
                cam_log.write(f"\n--- start {time.strftime('%H:%M:%S')} settle={clk}/{data} ---\n".encode())
                proc = subprocess.Popen(
                    [
                        "cam",
                        "-c",
                        str(idx),
                        "--stream",
                        f"role=raw,width={cap_w},height={cap_h}",
                        "--capture=0",
                        f"--file={pattern}",
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=cam_log,
                )
                print(f"CAMF: cam pid={proc.pid} continuous", flush=True)
                stream_t0 = time.monotonic()
                stable = 0
                consecutive = 0
                frames = 0
                n_fps = 0
                n_white = 0
                n_held = 0
                raw_seen = 0
                ae._drops = 0
                last_yuyv.clear()
                # Never wipe while a client is waiting — that write can block
                # the whole daemon and Discord stays black with LED on.
                time.sleep(0.05)
                continue
            # At most one cooled STREAMON retry. Same settle — never thrash pairs.
            # Zero RAW or sustained full-white both mean DPHY missed the lock.
            waited = time.monotonic() - stream_t0
            if start_retries < 1 and (
                (raw_seen == 0 and waited > 5.0)
                or (raw_seen >= 8 and frames == 0 and waited > 2.0)
            ):
                why = "zero RAW" if raw_seen == 0 else f"all-white raw={raw_seen}"
                _camf_log(f"CAMF {why} — one cooled retry (same settle)")
                _stop_proc(proc)
                proc = None
                start_retries += 1
                _note_csi_stop()
                continue
            if frames == 0 and int(waited) > 0 and int(waited) != getattr(run_front, "_wl", -1):
                run_front._wl = int(waited)
                _camf_log(
                    f"CAMF waiting picture {waited:.1f}s raw={raw_seen} "
                    f"white={n_white} alive={proc.poll() is None}"
                )
            need = _front_wh[0] * _front_wh[1] * 2
            files = sorted(shm.glob("*.raw"))
            path = None
            st_size = 0
            for cand in files:
                try:
                    st = cand.stat()
                except OSError:
                    continue
                if st.st_size >= need:
                    path = cand
                    st_size = st.st_size
                    break
            if path is None:
                if last_yuyv and active_nrs:
                    for nr in active_nrs:
                        fdw = fds.get(nr)
                        yuyv = last_yuyv.get(nr)
                        if fdw is not None and yuyv is not None:
                            write_yuyv(fdw, yuyv)
                time.sleep(0.005)
                continue
            # One wait for this file, not for every file in the directory.
            # write() can report the final size before the last bytes land.
            time.sleep(0.002)
            try:
                if path.stat().st_size != st_size:
                    continue
            except OSError:
                continue
            for old in files:
                if old == path:
                    break
                try:
                    old.unlink()
                except OSError:
                    pass
            try:
                buf = path.read_bytes()
                path.unlink(missing_ok=True)
            except OSError:
                continue
            arr = np.frombuffer(buf, dtype=np.uint16)
            fw, fh = _front_wh
            raw_seen += 1
            if arr.size < fw * fh:
                consecutive = 0
                if last_yuyv:
                    n_held += 1
                    for nr in active_nrs:
                        fdw = fds.get(nr)
                        yuyv = last_yuyv.get(nr)
                        if fdw is not None and yuyv is not None:
                            write_yuyv(fdw, yuyv)
                continue
            raw = arr[: fw * fh].reshape(fh, fw)
            sample = raw[::32, ::32]
            p50 = float(np.percentile(sample, 50))
            std = float(sample.std())
            sat_raw = float((sample >= 1000).mean())
            row_hi = (raw[::16, ::32] >= 1000).mean(axis=1)
            hard_rows = float((row_hi >= 0.98).mean())
            clean_rows = float((row_hi <= 0.05).mean())
            # Only reject unusable frames. Soft CSI glitter is held, not a cam kill.
            full_white = p50 > 980 or sat_raw > 0.70
            hard_tear = hard_rows >= 0.12 and clean_rows >= 0.20 and sat_raw >= 0.15
            black = p50 < 6 and std < 2.5
            if full_white or hard_tear or black:
                if full_white or hard_tear:
                    n_white += 1
                consecutive = 0
                if last_yuyv:
                    n_held += 1
                    for nr in active_nrs:
                        fdw = fds.get(nr)
                        yuyv = last_yuyv.get(nr)
                        if fdw is not None and yuyv is not None:
                            write_yuyv(fdw, yuyv)
                # No wipe once RAW is flowing — Discord would stay black.
                continue
            bgr = demosaic_front(buf)
            if bgr is None:
                continue
            g = cv2.cvtColor(bgr[::4, ::4], cv2.COLOR_BGR2GRAY)
            row_sat = (g >= 250).mean(axis=1)
            hard_b = float((row_sat >= 0.98).mean())
            clean_b = float((row_sat <= 0.05).mean())
            sat_b = float((g >= 250).mean())
            if hard_b >= 0.12 and clean_b >= 0.20 and sat_b >= 0.15:
                n_white += 1
                consecutive = 0
                if last_yuyv:
                    n_held += 1
                    for nr in active_nrs:
                        fdw = fds.get(nr)
                        yuyv = last_yuyv.get(nr)
                        if fdw is not None and yuyv is not None:
                            write_yuyv(fdw, yuyv)
                continue
            ae._drops = 0
            stable += 1
            consecutive += 1
            last_good = time.monotonic()
            # First usable frame publishes immediately (Discord must not sit on black).
            ae.update_bgr(bgr)
            frames += 1
            n_fps += 1
            now = time.monotonic()
            if now - t_fps >= 2.0:
                print(
                    f"CAMF real_fps={n_fps / (now - t_fps):.1f} "
                    f"frames={frames} white={n_white} held={n_held} "
                    f"exp={ae.exp} gain={ae.gain}",
                    flush=True,
                )
                t_fps = now
                n_fps = 0
                n_white = 0
                n_held = 0
            for nr in active_nrs:
                fdw = fds.get(nr)
                if fdw is None:
                    continue
                ow, oh = out_of[nr]
                frame = bgr if (bgr.shape[1] == ow and bgr.shape[0] == oh) else fit_profile(bgr, ow, oh)
                yuyv = bgr_to_yuyv(frame)
                last_yuyv[nr] = yuyv
                write_yuyv(fdw, yuyv)
    finally:
        _stop_proc(proc)
        _pm_qos_hold(False)
        ae.stop()
        for fdw in fds.values():
            try:
                os.close(fdw)
            except OSError:
                pass
        shutil.rmtree(shm, ignore_errors=True)
        print(f"CAMF done frames={frames}", flush=True)


def _ir_blank_bgr() -> np.ndarray:
    """Black loading frame. Clears v4l2loopback so Howdy/apps cannot see a
    stale face from the previous session while IR is still starting."""
    g = np.zeros((640, 480), dtype=np.uint8)
    return cv2.cvtColor(g, cv2.COLOR_GRAY2BGR)


def _ir_decode(buf: bytes) -> np.ndarray | None:
    arr = np.frombuffer(buf, dtype=np.uint16)
    if arr.size < 640 * 480:
        return None
    raw16 = arr[: 640 * 480].reshape(480, 640).astype(np.float32)
    # asinh keeps forehead texture; shadow lift + mild global gain for room.
    x = np.clip(raw16 / 1023.0, 0.0, 1.0)
    g = (np.arcsinh(x * 9.0) / np.arcsinh(9.0) * 255.0).astype(np.float32)
    g = np.ascontiguousarray(np.rot90(g, 1))
    lift = np.clip((120.0 - g) / 120.0, 0.0, 1.0) * 55.0
    g = np.clip(g + lift, 0, 255)
    g = np.clip(g * 1.12, 0, 255).astype(np.uint8)
    g = cv2.createCLAHE(1.15, (8, 8)).apply(g)
    if g.shape != (640, 480):
        g = cv2.resize(g, (480, 640))
    # Howdy dark_threshold looks at the whole frame. IR vignette is near-black
    # — raise the floor so hist[0] does not eat the scan budget.
    g = np.maximum(g, 36)
    bgr = cv2.cvtColor(g, cv2.COLOR_GRAY2BGR)
    if bgr.shape[1] != 480 or bgr.shape[0] != 640:
        bgr = cv2.resize(bgr, (480, 640))
    return bgr


def _ir_ae_step(sub: str, raw: np.ndarray, exp: list[int], gain: list[int]) -> None:
    """Face AE on R10. IR flood saturates fast — cut hard on sat."""
    c = raw[120:360, 160:480]
    p70 = float(np.percentile(c, 70))
    p95 = float(np.percentile(c, 95))
    sat = float((c >= 1000).mean())
    # Ignore LED re-arm / black frames — they make AE pump exposure into clip.
    if p95 < 80:
        return
    e, g = exp[0], gain[0]
    # Target face p70 ≈ 340–460 — a stop up from the too-dark pass.
    if sat > 0.015 or p95 >= 940 or p70 > 500:
        step = 80 if sat > 0.06 else 36
        e = max(60, e - step)
        if e <= 140:
            g = 16
    elif p70 < 320:
        e = min(420, e + 20)
        if e >= 360 and p70 < 260:
            g = min(24, g + 2)
    g = int(np.clip(g, 16, 24))
    e = int(np.clip(e, 60, 420))
    exp[0], gain[0] = e, g
    v4l_set(sub, f"exposure={e},analogue_gain={g}")


def run_ir(stop: threading.Event) -> None:
    """Howdy / face-resident hold /dev/video66. STREAMON only while they do."""
    idx = find_cam("CAM3")
    sub = find_subdev("ov7251")
    loop = find_loopback("Surface-IR-Howdy") or "/dev/video66"
    if idx is None or not Path(loop).exists():
        print("IR missing", flush=True)
        return
    try:
        fd = open_loopback(loop, 480, 640, fps=30)
    except OSError as e:
        print(f"IR loopback: {e}", flush=True)
        return
    # Root-only: PAM / face-resident open it. Apps must not see IR.
    try:
        os.chmod(loop, 0o600)
    except OSError:
        pass

    shm = Path("/dev/shm/surface-lc-ir")
    shm.mkdir(parents=True, exist_ok=True)
    pattern = str(shm / "f-#.raw")
    proc: subprocess.Popen | None = None
    led_stop = threading.Event()
    led_thr: threading.Thread | None = None
    last_clients = -1
    next_check = 0.0
    frames = 0
    ir_exp = [260]
    ir_gain = [16]
    print(f"CAMI ready cam={idx} LED off until Howdy opens {loop}", flush=True)

    blank_yuyv = bgr_to_yuyv(_ir_blank_bgr())

    def _wipe_loopback() -> None:
        try:
            write_yuyv(fd, blank_yuyv)
        except OSError:
            pass

    def _stop_ir() -> None:
        nonlocal proc, led_thr
        led_stop.set()
        if led_thr is not None:
            led_thr.join(timeout=2)
            led_thr = None
        _stop_proc(proc)
        proc = None
        ir_led(False)
        ov7251_power(False)
        _note_csi_stop()
        _wipe_loopback()
        for old in shm.glob("*.raw"):
            try:
                old.unlink()
            except OSError:
                pass

    try:
        _wipe_loopback()
        while not stop.is_set():
            now = time.monotonic()
            if now >= next_check or proc is None:
                next_check = now + 0.1
                clients = front_clients(loop)
                if clients != last_clients:
                    print(f"CAMI clients={clients}", flush=True)
                    last_clients = clients
            else:
                clients = last_clients
            if clients <= 0:
                if proc is not None:
                    print("CAMI no client → STREAMOFF (IR LED off)", flush=True)
                    _stop_ir()
                time.sleep(0.2)
                continue
            # RGB cameras must drop the ISYS before IR can lock.
            if any_external_clients(FRONT_LOOPBACKS + BACK_LOOPBACKS):
                if proc is not None:
                    print("CAMI wait for RGB release", flush=True)
                    _stop_ir()
                time.sleep(0.05)
                continue
            if proc is None or proc.poll() is not None:
                if proc is not None and proc.poll() is not None:
                    print(f"CAMI cam exit rc={proc.returncode}", flush=True)
                    _stop_ir()
                    time.sleep(0.3)
                for old in list(shm.glob("*.raw")):
                    try:
                        old.unlink()
                    except OSError:
                        pass
                # Wipe BEFORE STREAMON — otherwise Howdy matches the previous face.
                _wipe_loopback()
                ov7251_power(True)
                # 0x0140 + decode lift: face texture; 0x0180/0x02f4 wash it out.
                led_ok = ir_led(True, "0x0140")
                if not led_ok:
                    time.sleep(0.2)
                    led_ok = ir_led(True, "0x0140")
                print(f"CAMI start led={led_ok}", flush=True)
                led_stop.clear()

                def _led_hammer() -> None:
                    # Chip drops continuous-span unless refreshed; keep it rare.
                    while not led_stop.is_set():
                        if led_stop.wait(3.0):
                            break
                        ir_led(True, "0x0140")

                led_thr = threading.Thread(target=_led_hammer, daemon=True)
                led_thr.start()
                ir_exp[0], ir_gain[0] = 260, 16
                if sub:
                    v4l_set(sub, f"exposure={ir_exp[0]},analogue_gain={ir_gain[0]}")
                proc = subprocess.Popen(
                    [
                        "cam",
                        "-c",
                        str(idx),
                        "--stream",
                        "role=raw,width=640,height=480",
                        "--capture=0",
                        f"--file={pattern}",
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                print(f"CAMI: cam pid={proc.pid}", flush=True)
                frames = 0
                time.sleep(0.05)
                continue
            need = 640 * 480 * 2
            files = sorted(shm.glob("*.raw"))
            path = None
            for cand in files:
                try:
                    st = cand.stat()
                except OSError:
                    continue
                if st.st_size >= need:
                    path = cand
                    break
            if path is None:
                # Keep pushing blank so OpenCV cannot re-read a stale face.
                _wipe_loopback()
                time.sleep(0.002)
                continue
            for old in files:
                if old == path:
                    break
                try:
                    old.unlink()
                except OSError:
                    pass
            try:
                buf = path.read_bytes()
                path.unlink(missing_ok=True)
            except OSError:
                continue
            arr = np.frombuffer(buf, dtype=np.uint16)
            if arr.size < 640 * 480:
                continue
            raw = arr[: 640 * 480].reshape(480, 640)
            # LED-off / black: blank the loopback (do not leave the last face).
            if float(raw.mean()) < 40.0:
                _wipe_loopback()
                continue
            if sub and frames % 15 == 0:
                _ir_ae_step(sub, raw, ir_exp, ir_gain)
            bgr = _ir_decode(buf)
            if bgr is None:
                continue
            write_yuyv(fd, bgr_to_yuyv(bgr))
            frames += 1
            if frames == 1:
                try:
                    cv2.imwrite("/tmp/verify-ir.png", bgr)
                except Exception:
                    pass
            if frames % 60 == 0:
                print(
                    f"CAMI frames={frames} exp={ir_exp[0]} gain={ir_gain[0]}",
                    flush=True,
                )
    finally:
        _stop_ir()
        try:
            os.close(fd)
        except OSError:
            pass
        shutil.rmtree(shm, ignore_errors=True)
        print(f"CAMI done frames={frames}", flush=True)


def run_ir_burst(stop: threading.Event, seconds: float = 8.0) -> None:
    """One-shot IR capture for enroll / smoke tests."""
    deadline = time.monotonic() + seconds
    # Reuse the continuous path for a short window by holding the loopback.
    loop = find_loopback("Surface-IR-Howdy") or "/dev/video66"
    holder: int | None = None
    try:
        holder = os.open(loop, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        print(f"IR burst open: {e}", flush=True)
        return
    thr_stop = threading.Event()
    thr = threading.Thread(target=run_ir, args=(thr_stop,), daemon=True)
    thr.start()
    try:
        while not stop.is_set() and time.monotonic() < deadline:
            time.sleep(0.2)
    finally:
        thr_stop.set()
        thr.join(timeout=8)
        try:
            os.close(holder)
        except OSError:
            pass


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["back", "front", "ir", "ir-burst"], default="back")
    ap.add_argument("--cameras", default="")
    args = ap.parse_args()
    mode = args.mode
    if args.cameras.strip() == "CAM3":
        mode = "ir"

    stop = threading.Event()

    def _sig(*_):
        stop.set()

    signal.signal(signal.SIGINT, _sig)
    signal.signal(signal.SIGTERM, _sig)

    subprocess.run(
        [
            "bash",
            "-c",
            "chmod 660 /dev/media0 /dev/v4l-subdev* 2>/dev/null; "
            "chown root:video /dev/media0 /dev/v4l-subdev* 2>/dev/null; "
            "chmod 600 /dev/video{0..54} /dev/video6{1..5} /dev/video6{7..9} "
            "/dev/video70 2>/dev/null || true; "
            # Front+Back profiles readable for browser/Discord
            "chmod 666 /dev/video55 /dev/video56 /dev/video57 "
            "/dev/video58 /dev/video59 /dev/video60 2>/dev/null || true; "
            # IR hidden from apps
            "chmod 600 /dev/video66 2>/dev/null || true",
        ],
        check=False,
    )

    print(f"surface_lc_webcamd mode={mode}", flush=True)
    if mode == "ir":
        run_ir(stop)
    elif mode == "ir-burst":
        run_ir_burst(stop)
    elif mode == "front":
        run_front(stop)
    else:
        run_back(stop)
    return 0


if __name__ == "__main__":
    sys.exit(main())
