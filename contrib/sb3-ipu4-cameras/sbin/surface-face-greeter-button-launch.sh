#!/usr/bin/env bash
# Launch Face Login button in the greeter's Wayland session (surface-ipu4 only).
set -u
/usr/local/sbin/surface-face-button-visible.sh || exit 0

# Resolve greeter runtime from plasma-login-greeter / plasmalogin process.
pick_env() {
  local pid dir disp
  for proc in plasma-login-greeter plasmalogin-helper startplasma-login; do
    pid=$(pgrep -n -x "$proc" 2>/dev/null || true)
    [[ -n "$pid" ]] || continue
    dir=$(tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null | awk -F= '$1=="XDG_RUNTIME_DIR"{print $2; exit}')
    disp=$(tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null | awk -F= '$1=="WAYLAND_DISPLAY"{print $2; exit}')
    if [[ -n "$dir" ]]; then
      export XDG_RUNTIME_DIR="$dir"
      [[ -n "$disp" ]] && export WAYLAND_DISPLAY="$disp"
      return 0
    fi
  done
  # Fallback: first /run/user/*/wayland-*
  for dir in /run/user/*; do
    [[ -d "$dir" ]] || continue
    for sock in "$dir"/wayland-*; do
      [[ -S "$sock" ]] || continue
      export XDG_RUNTIME_DIR="$dir"
      export WAYLAND_DISPLAY="$(basename "$sock")"
      return 0
    done
  done
  return 1
}

mkdir -p /run/surface-howdy
chmod 1777 /run/surface-howdy 2>/dev/null || true

# Wait briefly for greeter display (never hang boot — max 8s).
for _ in $(seq 1 40); do
  pick_env && break
  sleep 0.2
done
pick_env || exit 0

exec /usr/local/bin/surface-face-greeter-button
