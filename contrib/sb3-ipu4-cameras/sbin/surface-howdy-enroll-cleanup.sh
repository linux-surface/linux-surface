#!/bin/bash
# Root helper: release IR after Howdy enroll (webcamd returns to idle/Front).
set -euo pipefail
MARKER=/run/surface-howdy
rm -f "$MARKER/enroll.pid" "$MARKER/disable" 2>/dev/null || true
echo idle > "$MARKER/state" 2>/dev/null || true
# Drop any leftover holders on the IR loopback except webcamd.
IR_DEV=""
[[ -f "$MARKER/device" ]] && IR_DEV=$(cat "$MARKER/device" 2>/dev/null || true)
if [[ -z "$IR_DEV" ]]; then
  for ent in /sys/class/video4linux/video*; do
    [[ -e "$ent/name" ]] || continue
    name=$(cat "$ent/name" 2>/dev/null || true)
    if [[ "$name" == "Surface-IR-Howdy" ]]; then
      IR_DEV="/dev/$(basename "$ent")"
      break
    fi
  done
fi
if [[ -n "$IR_DEV" ]] && command -v fuser >/dev/null; then
  # do not kill webcamd — only stray howdy/ffmpeg readers
  for pid in $(fuser "$IR_DEV" 2>/dev/null || true); do
    cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)
    if echo "$cmd" | grep -Eqi 'howdy|ffmpeg'; then
      kill "$pid" 2>/dev/null || true
    fi
  done
fi
echo "enroll cleanup done"
exit 0
