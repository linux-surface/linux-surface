#!/bin/bash
# Howdy IR dry-run (English). Validates IR frames + Howdy device path.
# GUI `howdy test` is optional when DISPLAY works; never abort on Qt failure under systemd.
set -euo pipefail

USER_NAME="${HOWDY_TEST_USER:-${SUDO_USER:-${USER:-}}}"
MARKER=/run/surface-howdy
mkdir -p "$MARKER" /tmp/camtest

IR=""
for e in /sys/class/video4linux/video*; do
  [[ "$(cat "$e/name" 2>/dev/null)" == "Surface-IR-Howdy" ]] || continue
  IR="/dev/$(basename "$e")"
  break
done
[[ -n "$IR" ]] || { echo "no IR loopback"; exit 1; }
echo "IR=$IR"
chmod 666 "$IR" 2>/dev/null || true

# Hold via howdy-named wrapper in /run (writable by root; parent chain has howdy)
HOLD_SH="$MARKER/surface-howdy-test-hold"
cat >"$HOLD_SH" <<EOF
#!/bin/bash
# surface-howdy-test-hold
python3 -c '
import os, time, select
fd = os.open("$IR", os.O_RDONLY | os.O_NONBLOCK)
end = time.time() + 40
try:
    while time.time() < end:
        st = open("/run/surface-webcam/status").read() if os.path.exists("/run/surface-webcam/status") else ""
        if "cam=ir" in st:
            import re
            m = re.search(r"n=(\d+)", st)
            if m and int(m.group(1)) >= 3:
                print("IR_READY", st.strip(), flush=True)
                break
        r, _, _ = select.select([fd], [], [], 0.4)
        if r:
            try: os.read(fd, 1920*1080*2)
            except BlockingIOError: pass
    else:
        print("IR_TIMEOUT", open("/run/surface-webcam/status").read() if os.path.exists("/run/surface-webcam/status") else "", flush=True)
        raise SystemExit(2)
    time.sleep(8)
finally:
    os.close(fd)
'
EOF
chmod +x "$HOLD_SH"
"$HOLD_SH" &
HOLD=$!

IR_READY=0
for i in $(seq 1 50); do
  st=$(cat /run/surface-webcam/status 2>/dev/null || true)
  echo "$st"
  if echo "$st" | grep -q 'cam=ir' && echo "$st" | grep -Eq 'n=[3-9]|n=[1-9][0-9]'; then
    IR_READY=1
    break
  fi
  sleep 0.4
done
[[ "$IR_READY" -eq 1 ]] || { echo "FAIL IR not ready"; kill "$HOLD" 2>/dev/null || true; exit 1; }

# Confirm blink path while howdy-named holder is open
led=$(cat /run/surface-webcam/ir_led_mode 2>/dev/null || true)
owner=$(cat /run/surface-webcam/ir_owner 2>/dev/null || true)
echo "ir_owner=$owner ir_led=$led"
[[ "$owner" == "howdy" ]] && echo "OK Howdy owns IR (mutex)" || echo "WARN ir_owner=$owner"
[[ "$led" == "blink" ]] && echo "OK LED blink mode" || echo "WARN ir_led=$led (want blink)"

# Headless OpenCV grab (no namedWindow) — proves Howdy recorder path
python3 - "$IR" <<'PY'
import sys
import cv2
dev = sys.argv[1]
# V4L2 index from /dev/videoN
idx = int(dev.replace("/dev/video", ""))
cap = cv2.VideoCapture(idx, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
ok = 0
for _ in range(30):
    ret, frame = cap.read()
    if ret and frame is not None and frame.size > 0:
        ok += 1
        if ok >= 5:
            break
cap.release()
print(f"opencv frames={ok} shape={getattr(frame, 'shape', None)}")
raise SystemExit(0 if ok >= 3 else 1)
PY

# Optional GUI howdy test — only when DISPLAY is a live X socket and not forced headless
set +e
if [[ -n "${DISPLAY:-}" && -S /tmp/.X11-unix/X${DISPLAY#:} ]] && [[ "${HOWDY_SKIP_GUI:-0}" != "1" ]]; then
  export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
  timeout 12 howdy --user "$USER_NAME" test >/tmp/camtest/howdy-gui.txt 2>&1 &
  TP=$!
  sleep 6
  kill "$TP" 2>/dev/null
  wait "$TP" 2>/dev/null
  if grep -qi 'Opening a window\|Howdy Test\|certainty' /tmp/camtest/howdy-gui.txt 2>/dev/null; then
    echo "OK howdy GUI test started"
  else
    echo "WARN howdy GUI test skipped/failed (use: sudo howdy --user $USER_NAME test in a desktop terminal)"
    tail -5 /tmp/camtest/howdy-gui.txt 2>/dev/null || true
  fi
else
  echo "skip GUI howdy test (no DISPLAY) — IR+opencv OK"
fi
set -e

pkill -P "$HOLD" 2>/dev/null || true
kill "$HOLD" 2>/dev/null || true
wait "$HOLD" 2>/dev/null || true

echo "OK howdy IR path ready"
exit 0
