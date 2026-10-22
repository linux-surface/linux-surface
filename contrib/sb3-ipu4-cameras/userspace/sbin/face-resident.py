#!/usr/bin/env python3
"""Keep Howdy's face models loaded and answer one scan at a time.

Started in parallel with the greeter, never as a dependency of it.
The PAM script asks this socket so a cold boot does not spend the
whole login loading dlib, and so the lock screen (which cannot open
the IR device itself) still gets a root-side scan.
"""
from __future__ import annotations

import json
import os
import socket
import sys
import time

HOWDY = "/usr/lib/python3.14/site-packages/howdy"
if HOWDY not in sys.path:
    sys.path.insert(0, HOWDY)

import configparser

import cv2
import dlib
import numpy as np

import paths_factory

SOCK = "/run/face-login/resident.sock"
# After IR STREAMON + LED arm, a usable lit frame needs ~1–2s. Keep enough
# budget after that for HOG + encoding (4s was too tight on cold open).
SCAN_S = 8.0


def load_detectors():
    face_detector = dlib.get_frontal_face_detector()
    pose = dlib.shape_predictor(paths_factory.shape_predictor_5_face_landmarks_path())
    encoder = dlib.face_recognition_model_v1(
        paths_factory.dlib_face_recognition_resnet_model_v1_path()
    )
    return face_detector, pose, encoder


def encodings_for(user: str) -> np.ndarray | None:
    path = paths_factory.user_model_path(user)
    try:
        models = json.load(open(path))
    except (OSError, json.JSONDecodeError):
        return None
    rows = []
    for model in models:
        rows.extend(model.get("data") or [])
    if not rows:
        return None
    return np.array(rows, dtype=np.float64)


def scan(user: str, detectors) -> int:
    face_detector, pose, encoder = detectors
    known = encodings_for(user)
    if known is None:
        return 10
    cfg = configparser.ConfigParser()
    cfg.read(paths_factory.config_file_path())
    device = cfg.get("video", "device_path", fallback="/dev/video66")
    certainty = cfg.getfloat("video", "certainty", fallback=3.5) / 10.0
    dark_threshold = cfg.getfloat("video", "dark_threshold", fallback=90.0)
    api = getattr(cv2, "CAP_V4L2", cv2.CAP_V4L)
    cap = cv2.VideoCapture(device, api)
    if not cap.isOpened():
        return 14
    fw = cfg.getint("video", "frame_width", fallback=-1)
    fh = cfg.getint("video", "frame_height", fallback=-1)
    if fw > 0:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, fw)
    if fh > 0:
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, fh)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))

    def _lit(frame_bgr):
        """Return (frame, gray) only when Howdy would accept the frame."""
        try:
            gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)
        except cv2.error:
            return None
        gray = clahe.apply(gray)
        hist = cv2.calcHist([gray], [0], None, [8], [0, 256])
        total = float(np.sum(hist))
        if total <= 0:
            return None
        darkness = float(np.ravel(hist)[0]) / total * 100.0
        if darkness >= 100.0 or darkness > dark_threshold:
            return None
        # Discard near-black LED-off frames (mean stays low until span arms).
        if float(gray.mean()) < 45.0:
            return None
        return frame_bgr, gray

    # Warmup waits for a lit LIVE frame. Reject the first reads: v4l2loopback
    # can still hold the previous unlock's face until IR writes a blank.
    opened_at = time.monotonic()
    warmup = opened_at + 8.0
    frame = None
    gray = None
    while time.monotonic() < warmup:
        ok, grabbed = cap.read()
        if ok and grabbed is not None and getattr(grabbed, "size", 0):
            # Ignore anything in the first 0.6s — stale loopback buffer.
            if time.monotonic() - opened_at < 0.6:
                time.sleep(0.03)
                continue
            lit = _lit(grabbed)
            if lit is not None:
                # Uniform gray blank (no face) has tiny std — skip it.
                if float(lit[1].std()) < 12.0:
                    time.sleep(0.03)
                    continue
                frame, gray = lit
                break
        time.sleep(0.03)
    if frame is None:
        cap.release()
        return 14
    deadline = time.monotonic() + SCAN_S
    try:
        while True:
            if frame is None or gray is None:
                if time.monotonic() >= deadline:
                    return 11
                ok, grabbed = cap.read()
                if not ok or grabbed is None or getattr(grabbed, "size", 0) == 0:
                    frame = None
                    gray = None
                    time.sleep(0.02)
                    continue
                lit = _lit(grabbed)
                if lit is None:
                    frame = None
                    gray = None
                    continue
                frame, gray = lit
            hit = False
            # upsample=1 fits the scan budget on 480x640 IR.
            for rect in face_detector(gray, 1):
                landmark = pose(frame, rect)
                found = np.array(encoder.compute_face_descriptor(frame, landmark, 1))
                dist = float(np.min(np.linalg.norm(known - found, axis=1)))
                if 0.0 < dist < certainty:
                    hit = True
                    break
            frame = None
            gray = None
            if hit:
                return 0
    finally:
        cap.release()


def serve() -> None:
    os.makedirs("/run/face-login", mode=0o755, exist_ok=True)
    os.makedirs("/run/face-login/viewers", mode=0o1777, exist_ok=True)
    try:
        os.chmod("/run/face-login/viewers", 0o1777)
    except OSError:
        pass
    try:
        os.unlink(SOCK)
    except OSError:
        pass
    detectors = load_detectors()
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(SOCK)
    os.chmod(SOCK, 0o660)
    try:
        import grp
        os.chown(SOCK, 0, grp.getgrnam("video").gr_gid)
    except (KeyError, OSError):
        pass
    srv.listen(2)
    srv.settimeout(1.0)
    print("face-resident ready", flush=True)
    while True:
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            continue
        with conn:
            conn.settimeout(2.0)
            user = "?"
            try:
                raw = b""
                while b"\n" not in raw and len(raw) < 64:
                    chunk = conn.recv(64)
                    if not chunk:
                        break
                    raw += chunk
                user = raw.split(b"\n", 1)[0].decode("utf-8", "ignore").strip()
                if not user or "/" in user or user.startswith("."):
                    code = 12
                else:
                    code = scan(user, detectors)
            except Exception as exc:
                print(f"scan error {exc}", flush=True)
                code = 1
            try:
                conn.sendall(f"{code}\n".encode())
            except OSError:
                pass
            print(f"scan {user} -> {code}", flush=True)


if __name__ == "__main__":
    serve()
