#!/usr/bin/env python3
"""Light the KDE camera icon for apps that open V4L2 directly.

The tray icon follows PipeWire nodes with media.role=Camera. Kamoso
streams through PipeWire, so the node goes Running and the icon appears.
Discord and the browser open /dev/videoN, the node stays suspended, and
the icon stays off. While webcamd reports a hard reader, open that same
PipeWire node so its state becomes Running. Stop as soon as the hard
reader is gone, otherwise this client would keep the sensor on.
"""
from __future__ import annotations

import json
import os
import signal
import subprocess
import time

STATUS_DIR = "/run/surface-webcam"
INDICATOR = os.path.join(STATUS_DIR, "indicator")
CARDS = {
    "front-standard": "Surface-Front-Standard",
    "front-hq": "Surface-Front-HQ",
    "front-fast": "Surface-Front-Fast",
    "back-standard": "Surface-Back-Standard",
    "back-hq": "Surface-Back-HQ",
    "back-fast": "Surface-Back-Fast",
}


def node_name(card: str) -> str | None:
    try:
        raw = subprocess.run(
            ["pw-dump"],
            capture_output=True,
            timeout=2,
            check=False,
        ).stdout
        data = json.loads(raw or b"[]")
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError):
        return None
    for obj in data:
        info = obj.get("info") or {}
        props = info.get("props") or {}
        if props.get("api.v4l2.cap.card") != card and props.get("node.description") != f"{card} (V4L2)":
            continue
        if props.get("media.class") != "Video/Source":
            continue
        name = props.get("node.name")
        if name:
            return str(name)
    return None


def main() -> None:
    proc: subprocess.Popen | None = None
    current = ""

    def stop() -> None:
        nonlocal proc, current
        if proc is not None and proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                proc.kill()
        proc = None
        current = ""

    while True:
        try:
            with open(INDICATOR, "r", encoding="utf-8") as fh:
                want = fh.read().strip()
        except OSError:
            want = "idle"
        if want not in CARDS:
            stop()
            time.sleep(0.4)
            continue
        if proc is not None and proc.poll() is not None:
            proc = None
            current = ""
        if current == want and proc is not None:
            time.sleep(0.4)
            continue
        stop()
        name = node_name(CARDS[want])
        if not name:
            time.sleep(0.4)
            continue
        proc = subprocess.Popen(
            [
                "gst-launch-1.0",
                "-q",
                "pipewiresrc",
                f"target-object={name}",
                "!",
                "fakesink",
                "sync=false",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        current = want
        time.sleep(0.4)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
