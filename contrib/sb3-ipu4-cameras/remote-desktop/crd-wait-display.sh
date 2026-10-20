#!/usr/bin/env bash
# Wait until the target user's Wayland compositor socket exists.
# Usage: crd-wait-display.sh <username>
set -euo pipefail
user="${1:-}"
if [[ -z "$user" ]]; then
  echo "usage: $0 <username>" >&2
  exit 1
fi
uid="$(id -u "$user")"
runtime="/run/user/$uid"
for _ in $(seq 1 60); do
  if [[ -S "$runtime/wayland-0" || -S "$runtime/wayland-1" ]]; then
    exit 0
  fi
  # Greeter / plasmalogin may publish under a different runtime — optional hint file
  if [[ -f /run/crd-greeter.env ]]; then
    # shellcheck disable=SC1091
    source /run/crd-greeter.env
    if [[ -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" && -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
      exit 0
    fi
  fi
  sleep 1
done
echo "No Wayland socket for $user after 60s" >&2
exit 1
