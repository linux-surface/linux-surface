#!/bin/bash
# Root helper: prepare IR for Howdy enroll via surface-webcamd (Surface-IR-Howdy).
set -euo pipefail
LOG=/var/log/surface-ir-howdy-enroll.log
MARKER=/run/surface-howdy
mkdir -p "$MARKER" "$(dirname "$LOG")"

resolve_ir() {
  for ent in /sys/class/video4linux/video*; do
    [[ -e "$ent/name" ]] || continue
    name=$(cat "$ent/name" 2>/dev/null || true)
    if [[ "$name" == "Surface-IR-Howdy" ]]; then
      echo "/dev/$(basename "$ent")"
      return 0
    fi
  done
  [[ -e /dev/video62 ]] && { echo /dev/video62; return 0; }
  [[ -e /dev/video60 ]] && { echo /dev/video60; return 0; }
  return 1
}

IR_DEV=$(resolve_ir) || { echo "Surface-IR-Howdy loopback missing"; exit 1; }
echo "ir_loopback=$IR_DEV"

systemctl start surface-webcam.service 2>/dev/null || true
# Ensure valid Howdy INI + PAM enabled before `howdy add`
if [[ -x /usr/local/sbin/configure-howdy-ir.sh ]]; then
  /usr/local/sbin/configure-howdy-ir.sh >>"$LOG" 2>&1 || true
else
  /usr/local/sbin/surface-howdy-gate.sh --enable >>"$LOG" 2>&1 || true
fi
# Give webcamd a moment after boot / after RGB STREAMOFF
sleep 1

chmod 666 "$IR_DEV" 2>/dev/null || true
: > "$LOG"
echo preparing > "$MARKER/state"

# Drop preview/browser holders so Howdy owns IR exclusively
if command -v fuser >/dev/null; then
  for pid in $(fuser "$IR_DEV" 2>/dev/null || true); do
    cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)
    if echo "$cmd" | grep -Eqi 'howdy|surface-howdy|surface_webcamd'; then
      continue
    fi
    echo "enroll mutex: dropping preview pid=$pid ($cmd)" | tee -a "$LOG"
    kill "$pid" 2>/dev/null || true
  done
fi
echo howdy > /run/surface-webcam/ir_owner 2>/dev/null || true

# Open the loopback so webcamd switches to IR (Howdy preempt).
python3 - "$IR_DEV" <<'PY' | tee -a "$LOG"
import os, sys, time
dev = sys.argv[1]
fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
try:
    deadline = time.time() + 25
    status = "/run/surface-webcam/status"
    while time.time() < deadline:
        st = ""
        try:
            st = open(status).read()
        except OSError:
            pass
        if "cam=ir" in st and "n=" in st:
            import re
            m = re.search(r"n=(\d+)", st)
            if m and int(m.group(1)) >= 3:
                print("IR_OK", st.strip())
                sys.exit(0)
        time.sleep(0.4)
    print("IR_FAIL", open(status).read().strip() if os.path.exists(status) else "no status")
    sys.exit(1)
finally:
    # keep fd open briefly so howdy can attach; webcamd linger covers the gap
    time.sleep(0.5)
    os.close(fd)
PY
rc=$?
if [[ $rc -eq 0 ]]; then
  echo streaming > "$MARKER/state"
  echo "$IR_DEV" > "$MARKER/device"
  echo IR_OK
  exit 0
fi
echo failed > "$MARKER/state"
exit 1
