#!/usr/bin/env bash
# Switch / preview / Howdy mutex smoke tests (English output).
# Uses real Python readers — v4l2-ctl is ignored by webcamd (SKIP_COMMS).
# Do NOT capture hold_reader via $() — subshell SIGHUP kills the background reader.
set -euo pipefail
mkdir -p /tmp/camtest
STATUS=/run/surface-webcam/status
OWNER=/run/surface-webcam/ir_owner
LEDMODE=/run/surface-webcam/ir_led_mode
HOLD_PID=0

resolve() {
  local want=$1
  for ent in /sys/class/video4linux/video*; do
    [[ -e "$ent/name" ]] || continue
    name=$(cat "$ent/name" 2>/dev/null || true)
    if [[ "$name" == "$want" ]]; then
      echo "/dev/$(basename "$ent")"
      return 0
    fi
  done
  return 1
}

FRONT=$(resolve Surface-Front)
BACK=$(resolve Surface-Back)
IR=$(resolve Surface-IR-Howdy)
echo "loopbacks: front=$FRONT back=$BACK ir=$IR"

wait_status() {
  local needle=$1
  local t=${2:-40}
  local i
  for i in $(seq 1 "$t"); do
    st=$(cat "$STATUS" 2>/dev/null || true)
    if [[ "$st" == *"$needle"* ]]; then
      echo "OK status: $st"
      return 0
    fi
    sleep 0.5
  done
  echo "FAIL want=$needle got=$(cat "$STATUS" 2>/dev/null || echo none)"
  return 1
}

hold_reader() {
  local dev=$1
  local secs=${2:-25}
  python3 - "$dev" "$secs" <<'PY' &
import os, sys, time, select
dev, secs = sys.argv[1], float(sys.argv[2])
fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
end = time.time() + secs
try:
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.4)
        if r:
            try:
                os.read(fd, 1920 * 1080 * 2)
            except BlockingIOError:
                pass
finally:
    os.close(fd)
PY
  HOLD_PID=$!
  disown "$HOLD_PID" 2>/dev/null || true
}

echo "=== 1) Front preview (program) ==="
hold_reader "$FRONT" 20
FP=$HOLD_PID
sleep 2
wait_status "cam=front" 20 || true
kill "$FP" 2>/dev/null || true
wait "$FP" 2>/dev/null || true
sleep 1

echo "=== 2) IR preview (browser/program) — LED should be steady ==="
hold_reader "$IR" 45
IP=$HOLD_PID
sleep 2
wait_status "cam=ir" 40
wait_status "ir_owner=preview" 20
steady_hits=0
for i in $(seq 1 10); do
  st=$(cat "$STATUS" 2>/dev/null || true)
  m=$(cat "$LEDMODE" 2>/dev/null || echo none)
  echo "  led_file=$m status_snip=$(echo "$st" | tr '\n' ' ' | grep -oE 'ir_owner=[^ ]+|ir_led=[^ ]+|cam=[^ ]+|ri=\[[^]]*\]' | tr '\n' ' ')"
  if [[ "$st" == *ir_led=steady* ]] || [[ "$m" == "steady" ]]; then
    steady_hits=$((steady_hits + 1))
  fi
  kill -0 "$IP" 2>/dev/null || { echo "FAIL IR preview reader died early"; exit 1; }
  sleep 0.4
done
[[ "$steady_hits" -ge 5 ]] && echo "OK preview LED steady ($steady_hits/10)" || {
  echo "FAIL steady hits=$steady_hits"
  kill "$IP" 2>/dev/null || true
  exit 1
}
kill "$IP" 2>/dev/null || true
wait "$IP" 2>/dev/null || true
sleep 2

echo "=== 3) Howdy mutex — Howdy-named reader → blink + exclusive ==="
hold_reader "$IR" 45
PRE=$HOLD_PID
sleep 2
wait_status "cam=ir" 40 || true
wait_status "ir_owner=preview" 20 || true

# Keep bash pathname with 'howdy' — webcamd walks parent cmdline chain
cat >/tmp/surface-howdy-hold-test <<EOF
#!/bin/bash
# surface-howdy-hold-test — cmdline must contain howdy for webcamd blink/mutex
python3 -c '
import os, time, select
fd = os.open("$IR", os.O_RDONLY | os.O_NONBLOCK)
end = time.time() + 25
try:
  while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.4)
    if r:
      try: os.read(fd, 1920*1080*2)
      except BlockingIOError: pass
finally:
  os.close(fd)
'
EOF
chmod +x /tmp/surface-howdy-hold-test
/tmp/surface-howdy-hold-test &
HP=$!
disown "$HP" 2>/dev/null || true
sleep 4
wait_status "ir_owner=howdy" 30
if kill -0 "$PRE" 2>/dev/null; then
  echo "FAIL: preview reader still alive after Howdy claimed IR (pid=$PRE)"
  kill "$PRE" "$HP" 2>/dev/null || true
  exit 1
else
  echo "OK mutex: preview reader dropped"
fi
blink_hits=0
for i in $(seq 1 12); do
  st=$(cat "$STATUS" 2>/dev/null || true)
  m=$(cat "$LEDMODE" 2>/dev/null || echo none)
  echo "  led_file=$m $(echo "$st" | tr '\n' ' ' | grep -oE 'ir_owner=[^ ]+|ir_led=[^ ]+' | tr '\n' ' ')"
  if [[ "$st" == *ir_led=blink* ]] || [[ "$m" == "blink" ]]; then
    blink_hits=$((blink_hits + 1))
  fi
  sleep 0.35
done
[[ "$blink_hits" -ge 3 ]] && echo "OK Howdy LED blink ($blink_hits/12)" || {
  echo "FAIL blink hits=$blink_hits"
  pkill -P "$HP" 2>/dev/null || true
  kill "$HP" 2>/dev/null || true
  exit 1
}
# Kill howdy bash + its python child (orphan python loses howdy parent → false preview)
pkill -P "$HP" 2>/dev/null || true
kill "$HP" 2>/dev/null || true
wait "$HP" 2>/dev/null || true
# Wait until IR fully released before Front
for i in $(seq 1 30); do
  st=$(cat "$STATUS" 2>/dev/null || true)
  [[ "$st" == *cam=idle* || "$st" == *cam=front* ]] && [[ "$st" != *ri=\[[1-9]* ]] && break
  # also accept empty ri
  echo "$st" | grep -q 'ri=\[\]' && echo "$st" | grep -Eq 'cam=(idle|front)' && break
  sleep 0.5
done
sleep 1

echo "=== 4) Back to Front after IR closed ==="
hold_reader "$FRONT" 20
FP=$HOLD_PID
sleep 2
wait_status "cam=front" 40 || echo "WARN front return"
kill "$FP" 2>/dev/null || true
wait "$FP" 2>/dev/null || true
sleep 2

echo "=== 5) Frame smoke on IR ==="
hold_reader "$IR" 20
IRP=$HOLD_PID
# Wait for IR stream with real frames
ok_frames=0
for i in $(seq 1 40); do
  st=$(cat "$STATUS" 2>/dev/null || true)
  if echo "$st" | grep -q 'cam=ir' && echo "$st" | grep -Eq 'n=[3-9]|n=[1-9][0-9]'; then
    ok_frames=1
    echo "OK IR streaming: $st"
    break
  fi
  sleep 0.5
done
kill "$IRP" 2>/dev/null || true
wait "$IRP" 2>/dev/null || true
[[ "$ok_frames" -eq 1 ]] || { echo "FAIL IR frame smoke"; exit 1; }

echo "DONE switch tests"
