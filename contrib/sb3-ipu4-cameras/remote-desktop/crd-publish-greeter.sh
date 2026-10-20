#!/usr/bin/env bash
# Publish greeter Wayland/DBus env to /run/crd-greeter.env for CRD host attach.
# Distro-neutral: no hard-coded login uid. Optional ACL via CRD_ACL_USER.
set -euo pipefail
OUT=/run/crd-greeter.env
umask 022

is_greeter_name() {
  case "${1:-}" in
    plasmalogin|sddm|gdm|lightdm|greeter) return 0 ;;
    *) return 1 ;;
  esac
}

# Skip when a real user already owns seat0 (desktop session active).
seat_is_desktop() {
  command -v loginctl >/dev/null 2>&1 || return 1
  local seat_uid seat_name
  seat_uid="$(loginctl show-seat seat0 -p ActiveUser --value 2>/dev/null || true)"
  [[ -n "$seat_uid" && "$seat_uid" != "0" ]] || return 1
  seat_name="$(getent passwd "$seat_uid" 2>/dev/null | cut -d: -f1 || true)"
  [[ -n "$seat_name" ]] || return 1
  if is_greeter_name "$seat_name"; then
    return 1
  fi
  return 0
}

pick_runtime() {
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

  # Fallback: Wayland socket owned by a known greeter account
  shopt -s nullglob
  for s in /run/user/*/wayland-0 /run/user/*/wayland-1; do
    [[ -S "$s" ]] || continue
    owner="$(stat -c %U "$s" 2>/dev/null || true)"
    if is_greeter_name "$owner"; then
      echo "$(dirname "$s")"
      shopt -u nullglob
      return 0
    fi
  done
  shopt -u nullglob

  # Last resort: system uid ranges typical for greeters
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

if seat_is_desktop; then
  rm -f "$OUT"
  exit 0
fi

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

bus=""
[[ -S "$runtime/bus" ]] && bus="unix:path=$runtime/bus"

desktop="${XDG_CURRENT_DESKTOP:-}"
[[ -z "$desktop" ]] && desktop="KDE"

# Optional: let CRD host user reach greeter runtime (Wayland + bus).
acl_user="${CRD_ACL_USER:-}"
if [[ -n "$acl_user" ]] && command -v setfacl >/dev/null 2>&1; then
  setfacl -m "u:${acl_user}:rwx" "$runtime" 2>/dev/null || true
  setfacl -d -m "u:${acl_user}:rwx" "$runtime" 2>/dev/null || true
  setfacl -m "u:${acl_user}:rw" "$runtime/$wayland" 2>/dev/null || true
  [[ -n "$bus" ]] && setfacl -m "u:${acl_user}:rw" "$runtime/bus" 2>/dev/null || true
fi

{
  echo "XDG_RUNTIME_DIR=$runtime"
  echo "WAYLAND_DISPLAY=$wayland"
  [[ -n "$bus" ]] && echo "DBUS_SESSION_BUS_ADDRESS=$bus"
  echo "XDG_SESSION_TYPE=wayland"
  echo "XDG_CURRENT_DESKTOP=$desktop"
} > "$OUT"
chmod 644 "$OUT"
