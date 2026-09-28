#!/bin/bash
# Before pam_howdy: nudge Surface-IR-Howdy so webcamd STREAMONs.
# IR warm from idle needs ~5–7s — wait up to 8s. Always exit 0.
set -u
LOG=/var/log/surface-ir-wait-auth.log
echo "$(date -Iseconds) wait-auth svc=${PAM_SERVICE:-?}" >>"$LOG" 2>/dev/null || true

for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
  if [[ -r /run/surface-webcam/status ]] || systemctl is-active --quiet surface-webcam.service 2>/dev/null; then
    break
  fi
  sleep 0.4
done

IR=""
for ent in /sys/class/video4linux/video*; do
  [[ -e "$ent/name" ]] || continue
  name=$(cat "$ent/name" 2>/dev/null || true)
  if [[ "$name" == "Surface-IR-Howdy" ]]; then
    IR="/dev/$(basename "$ent")"
    break
  fi
done
[[ -z "$IR" && -e /dev/video60 ]] && IR=/dev/video60
[[ -n "$IR" ]] || exit 0

echo ir > /run/surface-webcam/active 2>/dev/null || true

python3 - "$IR" <<'PY' >>"$LOG" 2>&1 || true
import os, sys, time
dev = sys.argv[1]
try:
    fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
except OSError as e:
    print(f"open fail {e}")
    raise SystemExit(0)
try:
    deadline = time.time() + 8.0
    status = "/run/surface-webcam/status"
    while time.time() < deadline:
        try:
            st = open(status).read()
        except OSError:
            st = ""
        if "cam=ir" in st:
            print("IR ready", st.strip()[:160])
            time.sleep(0.6)
            raise SystemExit(0)
        time.sleep(0.25)
    print("IR nudge timeout — pam_howdy will retry open")
finally:
    try:
        os.close(fd)
    except OSError:
        pass
PY
exit 0
