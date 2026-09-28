#!/usr/bin/env python3
"""Hard IPU4 ISYS guard — single owner, health-check, no SoftISP.

Usage:
  from ipu_guard import ipu_session
  with ipu_session("bridge-back") as g:
      g.require_healthy()
      ... media_setup / capture ...

Lock file: /run/surface-ipu/lock
State:     /run/surface-ipu/state
"""
from __future__ import annotations

import errno
import fcntl
import json
import os
import subprocess
import sys
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional

RUN = Path("/run/surface-ipu")
LOCK = RUN / "lock"
STATE = RUN / "state"
DEAD = RUN / "isys-dead"  # after a hung open — do not retry until reboot
# Capture nodes that must be openable (not I/O error)
# Current SB3 map: back=video0 (CSI0 cap0), front=video10 (CSI2 cap0)
HEALTH_NODES = ("/dev/video0", "/dev/video10", "/dev/media0")
# SoftISP userspace is forbidden; the PSYS module itself is OK (CSE/FW).
FORBIDDEN_MODULES: tuple[str, ...] = ()
# NEVER open() an IPU capture node from a long-lived process —
# dead ISYS can hang open() in D-state forever.



class IpuUnhealthy(RuntimeError):
    pass


class IpuBusy(RuntimeError):
    pass


def _ensure_run_dir() -> None:
    try:
        RUN.mkdir(parents=True, exist_ok=True)
    except PermissionError:
        # per-user fallback
        alt = Path.home() / ".cache" / "surface-ipu"
        alt.mkdir(parents=True, exist_ok=True)
        global LOCK, STATE
        LOCK = alt / "lock"
        STATE = alt / "state"


def psys_loaded() -> bool:
    try:
        text = Path("/proc/modules").read_text()
    except OSError:
        return False
    return any(m in text for m in FORBIDDEN_MODULES)


def node_ok(path: str, timeout_sec: float = 1.5) -> bool:
    """Open the node with a hard timeout — ISYS can hang open() forever."""
    if not os.path.exists(path):
        return False
    # subprocess + timeout: a signal in the parent does not wake a hung open in this process
    code = (
        "import os,sys\n"
        f"p={path!r}\n"
        "try:\n"
        " fd=os.open(p, os.O_RDONLY|os.O_NONBLOCK)\n"
        " os.close(fd)\n"
        " sys.exit(0)\n"
        "except OSError as e:\n"
        " sys.exit(50+e.errno)\n"
    )
    try:
        r = subprocess.run(
            [sys.executable, "-c", code],
            timeout=timeout_sec,
            capture_output=True,
        )
    except subprocess.TimeoutExpired:
        return False
    except Exception:
        return False
    if r.returncode == 0:
        return True
    err = r.returncode - 50 if r.returncode >= 50 else r.returncode
    if err in (errno.EIO, errno.ENODEV, errno.ENXIO):
        return False
    if err == errno.EBUSY:
        return True
    return False


def health_check() -> tuple[bool, str]:
    if psys_loaded():
        return False, "FORBIDDEN: intel_ipu4p_psys (SoftISP) is loaded — unload/blacklist it"
    if DEAD.exists():
        try:
            body = DEAD.read_text().strip()
        except OSError:
            body = "unreadable"
        return False, f"ISYS marked dead ({DEAD}) — {body}"
    missing = [p for p in HEALTH_NODES if not os.path.exists(p)]
    if missing:
        return False, f"missing nodes: {missing}"
    # Do not open video0/video10 — after 0-frame Front, open() needs a power cycle.
    return True, "ok"


def _mark_dead(reason: str) -> None:
    _ensure_run_dir()
    try:
        DEAD.write_text(f"{time.time()} {reason}\n")
    except OSError:
        pass


def clear_dead_marker() -> None:
    """Only after a successful reboot / deliberate ISYS recovery."""
    try:
        DEAD.unlink(missing_ok=True)  # type: ignore[call-arg]
    except TypeError:
        try:
            if DEAD.exists():
                DEAD.unlink()
        except OSError:
            pass
    except OSError:
        pass


def require_healthy() -> None:
    ok, msg = health_check()
    if not ok:
        raise IpuUnhealthy(msg)


def write_state(owner: str, phase: str, **extra) -> None:
    _ensure_run_dir()
    data = {"owner": owner, "phase": phase, "ts": time.time(), **extra}
    try:
        STATE.write_text(json.dumps(data) + "\n")
    except OSError:
        pass


def release_media() -> None:
    """Best-effort media-ctl reset — never raises."""
    try:
        subprocess.run(
            ["media-ctl", "-d", "/dev/media0", "-r"],
            check=False,
            capture_output=True,
            timeout=3,
        )
    except Exception:
        pass


def kill_stray_captures() -> None:
    """Kill stuck v4l2-ctl stream-to / RAW bridges (not howdy loopback ffmpeg with testsrc)."""
    for ent in Path("/proc").iterdir():
        if not ent.name.isdigit():
            continue
        try:
            cmd = (ent / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="ignore")
        except OSError:
            continue
        pid = int(ent.name)
        if pid == os.getpid():
            continue
        # only IPU capture (stream-to raw); do not blindly kill howdy on video60
        if "v4l2-ctl" in cmd and "stream-to" in cmd and (
            "surface-bridge" in cmd
            or "surface-triple" in cmd
            or "/dev/shm/surface" in cmd
            or "/tmp/surface" in cmd
        ):
            try:
                os.kill(pid, 9)
            except OSError:
                pass


@dataclass
class IpuGuard:
    owner: str
    _fd: Optional[int] = None

    def require_healthy(self) -> None:
        require_healthy()

    def release_pipeline(self) -> None:
        kill_stray_captures()
        release_media()
        write_state(self.owner, "idle")


@contextmanager
def ipu_session(owner: str, timeout_sec: float = 30.0) -> Iterator[IpuGuard]:
    """Exclusive IPU access. Always tears the pipeline down on exit."""
    _ensure_run_dir()
    require_healthy()
    fd = os.open(str(LOCK), os.O_CREAT | os.O_RDWR, 0o666)
    deadline = time.time() + timeout_sec
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.time() >= deadline:
                os.close(fd)
                raise IpuBusy(f"IPU busy (lock held) owner_waited={timeout_sec}s")
            time.sleep(0.1)
    g = IpuGuard(owner=owner, _fd=fd)
    write_state(owner, "acquired")
    try:
        require_healthy()
        kill_stray_captures()
        release_media()
        yield g
    finally:
        try:
            g.release_pipeline()
        finally:
            try:
                fcntl.flock(fd, fcntl.LOCK_UN)
            except OSError:
                pass
            os.close(fd)
            write_state(owner, "released")


def assert_safe_resolution(cam: str, w: int, h: int) -> None:
    """Reject known-risky modes (front full-res often hangs STREAMON/ISYS)."""
    if cam == "front" and (w, h) == (2592, 1944):
        raise IpuUnhealthy(
            "REFUSED: front 2592x1944 is known to stress/hang ISYS — use 1296x972"
        )
    if cam == "back" and (w, h) == (3264, 2448):
        raise IpuUnhealthy(
            "REFUSED: back 3264x2448 stress mode — use 800x600 or 1632x1224"
        )
