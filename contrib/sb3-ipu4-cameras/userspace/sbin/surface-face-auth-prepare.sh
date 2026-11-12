#!/usr/bin/env bash
# Millisecond IR prepare for on-demand Face Login (never block boot/greeter).
# Safe to call from root or from a path-triggered oneshot. Always exits 0.
set -u
LOG=/var/log/surface-face-auth.log
mkdir -p /run/surface-howdy
chmod 1777 /run/surface-howdy 2>/dev/null || true
echo "$(date -Iseconds) prepare start" >>"$LOG" 2>/dev/null || true

# Only the camera kernel — stock kernels must never touch Howdy/IR.
if ! uname -r | grep -q 'surface-ipu4'; then
  echo "skip: not surface-ipu4" >>"$LOG" 2>/dev/null || true
  exit 0
fi

IR=""
for ent in /sys/class/video4linux/video*; do
  [[ -e "$ent/name" ]] || continue
  name=$(cat "$ent/name" 2>/dev/null || true)
  if [[ "$name" == "Surface-IR-Howdy" ]]; then
    IR="/dev/$(basename "$ent")"
    break
  fi
done

if [[ -n "$IR" && -x /usr/local/sbin/surface-howdy-gate.sh ]]; then
  /usr/local/sbin/surface-howdy-gate.sh --enable >>"$LOG" 2>&1 || true
elif [[ -n "$IR" && -f /etc/howdy/config.ini ]]; then
  # Minimal path refresh without rewriting disabled unexpectedly
  python3 - "$IR" <<'PY' >>"$LOG" 2>&1 || true
import sys
from pathlib import Path
import configparser
ir = sys.argv[1]
p = Path("/etc/howdy/config.ini")
c = configparser.ConfigParser()
c.read(p)
if "video" not in c:
    c["video"] = {}
if "core" not in c:
    c["core"] = {}
c["video"]["device_path"] = ir
c["core"]["disabled"] = "false"
with p.open("w") as f:
    c.write(f)
print("device_path", ir)
PY
fi
rm -f /etc/howdy/force-disabled 2>/dev/null || true

# Nudge webcamd STREAMON — open loopback briefly (non-blocking).
if [[ -n "$IR" ]]; then
  echo ir > /run/surface-webcam/active 2>/dev/null || true
  python3 - "$IR" <<'PY' >>"$LOG" 2>&1 || true
import os, sys, time
dev = sys.argv[1]
try:
    fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
except OSError as e:
    print("open fail", e)
    raise SystemExit(0)
try:
    deadline = time.time() + 0.4
    while time.time() < deadline:
        try:
            st = open("/run/surface-webcam/status").read()
        except OSError:
            st = ""
        if "cam=ir" in st:
            print("IR ready", st.strip()[:120])
            break
        time.sleep(0.05)
finally:
    try:
        os.close(fd)
    except OSError:
        pass
PY
fi

echo "$(date -Iseconds) prepare done ir=${IR:-none}" >>"$LOG" 2>/dev/null || true
exit 0
