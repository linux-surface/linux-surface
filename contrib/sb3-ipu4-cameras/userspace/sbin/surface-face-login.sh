#!/usr/bin/env bash
# On-demand Face Login (root). Triggered by path unit after the greeter button
# writes /run/surface-howdy/face-request. Never runs on non-ipu4 kernels.
set -u
LOG=/var/log/surface-face-auth.log
REQ=/run/surface-howdy/face-request
mkdir -p /run/surface-howdy
echo "$(date -Iseconds) face-login start" >>"$LOG" 2>/dev/null || true

cleanup_req() { rm -f "$REQ" 2>/dev/null || true; }
trap cleanup_req EXIT

if ! uname -r | grep -q 'surface-ipu4'; then
  echo "refuse: not surface-ipu4" >>"$LOG" 2>/dev/null || true
  exit 0
fi

USER_NAME=""
if [[ -f "$REQ" ]]; then
  USER_NAME=$(head -n1 "$REQ" | tr -cd '[:alnum:]_.-')
fi
if [[ -z "$USER_NAME" ]]; then
  USER_NAME=$(grep -h '^User=' /etc/plasmalogin.conf /etc/plasmalogin.conf.d/* 2>/dev/null \
    | awk -F= '/./{print $2; exit}' | tr -cd '[:alnum:]_.-' || true)
fi
if [[ -z "$USER_NAME" || "$USER_NAME" == "root" ]]; then
  for m in /etc/howdy/models/*.dat; do
    [[ -f "$m" ]] || continue
    USER_NAME=$(basename "$m" .dat)
    break
  done
fi
[[ -n "$USER_NAME" ]] || USER_NAME=popos

if ! id "$USER_NAME" >/dev/null 2>&1; then
  echo "bad user $USER_NAME" >>"$LOG" 2>/dev/null || true
  exit 0
fi
if [[ ! -f "/etc/howdy/models/${USER_NAME}.dat" ]]; then
  echo "no model for $USER_NAME" >>"$LOG" 2>/dev/null || true
  exit 0
fi

/usr/local/sbin/surface-face-auth-prepare.sh || true

HOWDY_BIN=$(command -v howdy || true)
if [[ -z "$HOWDY_BIN" ]]; then
  echo "howdy missing" >>"$LOG" 2>/dev/null || true
  exit 0
fi

echo "scanning user=$USER_NAME" >>"$LOG" 2>/dev/null || true
if ! "$HOWDY_BIN" --user "$USER_NAME" 2>>"$LOG"; then
  echo "howdy failed rc=$?" >>"$LOG" 2>/dev/null || true
  exit 0
fi

echo "match ok — oneshot autologin $USER_NAME" >>"$LOG" 2>/dev/null || true
python3 - "$USER_NAME" <<'PY' >>"$LOG" 2>&1 || true
import sys
from pathlib import Path
user = sys.argv[1]
p = Path("/etc/plasmalogin.conf.d/10-surface-face.conf")
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(
    "[Autologin]\n"
    f"User={user}\n"
    "Session=plasma\n"
    "Relogin=false\n"
    "# surface-face-oneshot\n"
    "\n"
    "[Users]\n"
    "RememberLastUser=true\n"
    "RememberLastSession=true\n"
)
print("wrote", p)
PY

# Bounce greeter so oneshot Autologin applies. --no-block: never hang callers.
if command -v systemctl >/dev/null; then
  systemctl restart --no-block plasmalogin.service 2>>"$LOG" || \
    systemctl restart --no-block display-manager.service 2>>"$LOG" || true
fi
exit 0
