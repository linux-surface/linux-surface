#!/usr/bin/env bash
# Wait briefly for greeter or user Wayland before CRD starts.
# Stay under systemd TimeoutStartSec (default 45s): exit 0 on timeout.
# Usage: crd-wait-display.sh [username]
set -uo pipefail

user="${1:-}"
uid=""
if [[ -n "$user" ]]; then
  uid="$(id -u "$user" 2>/dev/null || true)"
fi

socket_ok() {
  local s="$1"
  [[ -n "$s" && -S "$s" ]]
}

is_greeter_owner() {
  case "${1:-}" in
    plasmalogin|sddm|gdm|lightdm|greeter) return 0 ;;
    *) return 1 ;;
  esac
}

for _ in $(seq 1 35); do
  if [[ -r /run/crd-greeter.env ]]; then
    # shellcheck disable=SC1091
    source /run/crd-greeter.env
    if [[ -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]] \
      && socket_ok "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}"; then
      exit 0
    fi
  fi

  if [[ -n "$uid" ]]; then
    shopt -s nullglob
    for s in /run/user/"$uid"/wayland-*; do
      [[ "$s" == *.lock ]] && continue
      socket_ok "$s" && exit 0
    done
    shopt -u nullglob
  fi

  shopt -s nullglob
  for s in /run/user/*/wayland-0 /run/user/*/wayland-1; do
    [[ "$s" == *.lock ]] && continue
    socket_ok "$s" || continue
    owner="$(stat -c %U "$s" 2>/dev/null || true)"
    if is_greeter_owner "$owner"; then
      exit 0
    fi
    if [[ -n "$user" && "$owner" == "$user" ]]; then
      exit 0
    fi
  done
  shopt -u nullglob

  sleep 1
done

# Do not fail the host unit — path/follow units can restart when a display appears.
exit 0
