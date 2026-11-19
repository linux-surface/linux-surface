#!/bin/bash
# Destroy ONLY bad V4L2 Device objects (IPU RAW / Surface spa-v4l2).
# NEVER touch Audio/* — a broad destroy here kills sinks/mics system-wide.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=${XDG_RUNTIME_DIR}/bus}"
for i in 1 2 3 4 5 6 7 8 9 10; do
  pw-cli ls Device >/dev/null 2>&1 && break
  sleep 0.5
done
python3 - <<'PY'
import json, subprocess, time

def destroy_bad():
    try:
        raw = subprocess.check_output(["pw-dump"], text=True, timeout=15)
    except Exception:
        return 0
    n = 0
    for o in json.loads(raw):
        if o.get("type") != "PipeWire:Interface:Device":
            continue
        p = o.get("info", {}).get("props", {})
        # Hard safety: never destroy audio/Bluetooth/etc.
        media = (p.get("media.class") or "")
        api = (p.get("device.api") or p.get("api.alsa.path") or "")
        if media.startswith("Audio/") or "alsa" in api.lower() or p.get("api.alsa.card"):
            continue
        if p.get("device.api") == "alsa" or "bluez" in (p.get("device.name") or ""):
            continue
        desc = p.get("device.description", "") or ""
        name = p.get("device.name", "") or ""
        driver = p.get("api.v4l2.cap.driver", "") or ""
        path = p.get("api.v4l2.path", "") or ""
        card = p.get("api.v4l2.cap.card", "") or ""
        # Must look like a V4L2 device we intentionally hide.
        is_v4l = bool(driver or path.startswith("/dev/video") or "v4l" in name.lower())
        if not is_v4l:
            continue
        # RGB loopbacks are video55–60. Destroying them emptied the
        # browser/Kamoso camera list. Hide only raw IPU and IR.
        bad = (
            desc.startswith("Surface-IR")
            or desc.startswith("Intel-IPU")
            or "IPU4" in desc
            or "IPU6" in desc
            or desc.lower().startswith("ipu4")
            or "ipu4" in driver
            or "ipu6" in driver
            or driver.startswith("intel-ipu")
            or driver == "ipu4p"
            or "ipu" in name.lower()
            or path == "/dev/video66"
            or (
                path.startswith("/dev/video")
                and path[10:].isdigit()
                and int(path[10:]) < 55
            )
            or card.startswith("Intel IPU")
            or "v4l-subdev" in path
        )
        if bad:
            subprocess.run(
                ["pw-cli", "destroy", str(o["id"])],
                capture_output=True,
                timeout=5,
            )
            n += 1
    return n

total = 0
for _ in range(4):
    total += destroy_bad()
    time.sleep(0.5)
print(f"destroyed={total}", flush=True)
PY
