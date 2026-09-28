#!/bin/bash
# Howdy gate for Surface IR (Surface-IR-Howdy loopback).
#
# Default (no args): refresh device_path only — never rewrite disabled.
#   --enable   set core.disabled=false (after webcamd / enroll)
#   --disable  set core.disabled=true  (emergency / greeter-before-IPU)
#
# Boot: surface-howdy-enable.service calls --enable after surface-webcam.
# Do NOT force disabled=true on every boot — that made PAM face auth a no-op.
set -euo pipefail
CFG=/etc/howdy/config.ini
[ -f "$CFG" ] || exit 0

MODE=refresh
[[ "${1:-}" == "--enable" ]] && MODE=enable
[[ "${1:-}" == "--disable" ]] && MODE=disable

IR_DEV=""
for ent in /sys/class/video4linux/video*; do
  [[ -e "$ent/name" ]] || continue
  name=$(cat "$ent/name" 2>/dev/null || true)
  if [[ "$name" == "Surface-IR-Howdy" ]]; then
    IR_DEV="/dev/$(basename "$ent")"
    break
  fi
done
[[ -z "$IR_DEV" && -e /dev/video60 ]] && IR_DEV=/dev/video60

python3 - "$CFG" "$IR_DEV" "$MODE" <<'PY'
import sys
from pathlib import Path
import configparser

cfg_path, ir, mode = sys.argv[1], sys.argv[2], sys.argv[3]
p = Path(cfg_path)
raw = p.read_text() if p.exists() else ""

# Recover from flat key dumps (no section headers) produced by old scripts.
if raw and "[core]" not in raw and "[video]" not in raw:
    raw = ""

c = configparser.ConfigParser()
if raw.strip():
    c.read_string(raw)
else:
    # Minimal valid skeleton — configure-howdy-ir.sh writes the full file.
    c["core"] = {"disabled": "false"}
    c["video"] = {}

if "core" not in c:
    c["core"] = {}
if "video" not in c:
    c["video"] = {}

if ir:
    c["video"]["device_path"] = ir

if mode == "enable":
    c["core"]["disabled"] = "false"
elif mode == "disable":
    c["core"]["disabled"] = "true"
# refresh: leave disabled alone

with p.open("w") as f:
    c.write(f)

dis = c["core"].get("disabled", "?")
dev = c["video"].get("device_path", "?")
print(f"howdy PAM: mode={mode} disabled={dis} device_path={dev}")
PY

if [[ "$MODE" == "enable" ]]; then
  rm -f /etc/howdy/force-disabled
fi
exit 0
