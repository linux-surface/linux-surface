#!/usr/bin/env bash
# Publish greeter Wayland/DBus env to /run/crd-greeter.env for CRD host attach.
# Avoids hard-coding any login uid; uses the active seat0 greeter session when present.
set -euo pipefail
OUT=/run/crd-greeter.env
umask 022

pick_runtime() {
  # Prefer seat0 sessions of class greeter
  if command -v loginctl >/dev/null 2>&1; then
    while read -r sid; do
      [[ -z "$sid" ]] && continue
      class="$(loginctl show-session "$sid" -p Class --value 2>/dev/null || true)"
      typ="$(loginctl show-session "$sid" -p Type --value 2>/dev/null || true)"
      uid="$(loginctl show-session "$sid" -p User --value 2>/dev/null || true)"
      if [[ "$class" == "greeter" && "$typ" == "wayland" && -n "$uid" ]]; then
        echo "/run/user/$uid"
        return 0
      fi
    done < <(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $1}')
  fi
  # Fallback: any /run/user/*/wayland-* owned by a system uid (>=900 typical for greeters)
  for d in /run/user/*; do
    [[ -d "$d" ]] || continue
    uid="${d##*/}"
    [[ "$uid" =~ ^[0-9]+$ ]] || continue
    if (( uid >= 900 && uid < 1000 )) || (( uid > 65000 )); then
      if compgen -G "$d/wayland-*" >/dev/null; then
        echo "$d"
        return 0
      fi
    fi
  done
  return 1
}

runtime="$(pick_runtime || true)"
if [[ -z "${runtime:-}" ]]; then
  rm -f "$OUT"
  exit 0
fi

wayland=""
for cand in wayland-0 wayland-1; do
  if [[ -S "$runtime/$cand" ]]; then
    wayland="$cand"
    break
  fi
done
[[ -n "$wayland" ]] || { rm -f "$OUT"; exit 0; }

bus="unix:path=$runtime/bus"
[[ -S "$runtime/bus" ]] || bus=""

{
  echo "XDG_RUNTIME_DIR=$runtime"
  echo "WAYLAND_DISPLAY=$wayland"
  [[ -n "$bus" ]] && echo "DBUS_SESSION_BUS_ADDRESS=$bus"
  echo "XDG_SESSION_TYPE=wayland"
} > "$OUT"
chmod 644 "$OUT"
