#!/usr/bin/env bash
# Exit 0 only when Face Login button should appear (camera kernel + enrolled model).
set -u
uname -r | grep -q 'surface-ipu4' || exit 1
# At least one Howdy model
shopt -s nullglob
models=(/etc/howdy/models/*.dat)
(( ${#models[@]} > 0 )) || exit 1
# IR loopback present (webcamd up) — optional soft check
for ent in /sys/class/video4linux/video*; do
  [[ -e "$ent/name" ]] || continue
  if [[ "$(cat "$ent/name" 2>/dev/null)" == "Surface-IR-Howdy" ]]; then
    exit 0
  fi
done
# Still show the button if models exist; prepare will no-op until cameras ready
exit 0
