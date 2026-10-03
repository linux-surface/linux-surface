#!/usr/bin/env bash
# PAM hook for universal face login. Exit 0 only when Howdy matches.
# pam_exec is installed as [success=done default=ignore], so every other
# exit leaves the password result alone.
#
# Runs only after pam_unix has already rejected the token. A non-empty
# token is a wrong password: do not open the camera. An empty token on an
# account that has a password starts Howdy, unless this attempt is remote
# or a viewer is connected to the seat right now.
set -u

LOG=/run/face-login/auth.log
MAX_SEC=16

log() {
  local line
  line="$(printf '%s %s' "$(date -Iseconds 2>/dev/null || date)" "$*")"
  printf '%s\n' "$line" >&2
  printf '%s\n' "$line" >>"$LOG" 2>/dev/null || true
}

# pam_exec expose_authtok writes the password to stdin. Never log it.
token=""
IFS= read -r token || true
if [[ -n "$token" ]]; then
  exit 1
fi

user="${PAM_USER:-}"
service="${PAM_SERVICE:-}"
[[ -n "$user" && "$user" != "root" ]] || exit 1

case "$service" in
  sshd|chrome-remote-desktop|cockpit|crond|cron|sudo|sudo-i|su|su-l|polkit-1|polkit|systemd-user|passwd|chsh|chfn|runuser)
    log "skip service=$service user=$user"
    exit 1
    ;;
esac

rhost="${PAM_RHOST:-}"
case "$rhost" in
  ""|localhost|127.0.0.1|::1) ;;
  *)
    log "skip rhost=$rhost service=$service user=$user"
    exit 1
    ;;
esac

# A connected viewer, not a host that is only listening.
# Any tool, on any distro, Wayland or X11: while a client is attached,
# keep a file in /run/face-login/viewers/<name> and remove it on disconnect.
# Chrome Remote Desktop also uses $XDG_RUNTIME_DIR/crd-viewer.
# logind Remote=yes alone is the listening host (no seat). A real remote
# person has RemoteHost set, or the session is sshd.
viewer_connected() {
  local f sid remote state host svc
  shopt -s nullglob
  for f in /run/face-login/viewers/* /run/user/*/crd-viewer; do
    [[ -e "$f" ]] || continue
    # Ignore the directory itself if the glob hits a dangling pattern.
    [[ -f "$f" ]] || continue
    log "skip viewer $f"
    return 0
  done
  shopt -u nullglob
  command -v loginctl >/dev/null 2>&1 || return 1
  local sessions
  sessions="$(timeout 2 loginctl list-sessions --no-legend 2>/dev/null | awk '{print $1}')" || true
  for sid in $sessions; do
    [[ "$sid" =~ ^[0-9]+$ ]] || continue
    remote="$(timeout 2 loginctl show-session "$sid" -p Remote --value 2>/dev/null || true)"
    state="$(timeout 2 loginctl show-session "$sid" -p State --value 2>/dev/null || true)"
    [[ "$remote" == "yes" && "$state" != "closing" ]] || continue
    host="$(timeout 2 loginctl show-session "$sid" -p RemoteHost --value 2>/dev/null || true)"
    svc="$(timeout 2 loginctl show-session "$sid" -p Service --value 2>/dev/null || true)"
    case "$host" in
      ""|localhost|127.0.0.1|::1)
        case "$svc" in
          ssh|sshd)
            log "skip ssh session=$sid"
            return 0
            ;;
        esac
        log "ignore listening host session=$sid service=${svc:-?} host=${host:-empty}"
        ;;
      *)
        log "skip remote peer session=$sid host=$host service=${svc:-?}"
        return 0
        ;;
    esac
  done
  return 1
}

if viewer_connected; then
  exit 1
fi

# pam_unix nullok already accepted an account with no password, so an
# empty token only reaches this script when the account has one.
# The greeter often cannot read /etc/shadow. An empty getent result is
# not "no password" and must not skip the scan.
hash="$(getent shadow "$user" 2>/dev/null | awk -F: '{print $2; exit}' || true)"
if [[ -n "$hash" ]]; then
  case "$hash" in
    "!"|"*"|"!!"|!* )
      log "skip locked user=$user"
      exit 1
      ;;
  esac
fi

model="/etc/howdy/models/${user}.dat"
if [[ ! -f "$model" ]]; then
  log "skip no model user=$user"
  exit 1
fi

device=""
if [[ -r /etc/howdy/config.ini ]]; then
  device="$(awk -F= '/^[[:space:]]*device_path[[:space:]]*=/{gsub(/[[:space:]]/,"",$2); print $2; exit}' /etc/howdy/config.ini || true)"
fi
if [[ -z "$device" || ! -e "$device" ]]; then
  log "skip no device user=$user device=${device:-missing}"
  exit 1
fi

compare=""
for compare in /usr/lib/python3.*/site-packages/howdy/compare.py; do
  [[ -f "$compare" ]] && break
  compare=""
done
if [[ -z "$compare" ]]; then
  log "skip howdy compare missing"
  exit 1
fi

if mkdir -p /run/face-login 2>/dev/null && : >>/run/face-login/scan.lock 2>/dev/null; then
  exec 9<>/run/face-login/scan.lock
  if ! flock -n 9; then
    log "skip scan already running user=$user"
    exit 1
  fi
fi

# The resident already has the models loaded and opens the IR camera as
# root, so the lock screen (uid of the session) can scan too.
ask_resident() {
  python3 - "$user" <<'PY'
import socket, sys
user = sys.argv[1]
path = "/run/face-login/resident.sock"
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(12)
try:
    s.connect(path)
    s.sendall(user.encode() + b"\n")
    data = b""
    while b"\n" not in data and len(data) < 16:
        chunk = s.recv(16)
        if not chunk:
            break
        data += chunk
except OSError:
    sys.exit(2)
code = data.split(b"\n", 1)[0].decode("utf-8", "ignore").strip()
sys.exit(0 if code == "0" else (1 if code in ("11", "13") else 2))
PY
}

set +e
ask_resident
rc=$?
set -e
if [[ "$rc" -eq 0 ]]; then
  log "match resident user=$user"
  exit 0
fi
if [[ "$rc" -eq 1 ]]; then
  log "no match resident user=$user"
  exit 1
fi

log "scan user=$service/$user device=$device"
set +e
timeout -k 1 "$MAX_SEC" python3 "$compare" "$user" >/tmp/face-pam-compare.out 2>&1
rc=$?
set -e
if [[ -r /tmp/face-pam-compare.out ]]; then
  cat /tmp/face-pam-compare.out >>"$LOG" 2>/dev/null || true
fi
if [[ "$rc" -eq 0 ]]; then
  log "match user=$user"
  exit 0
fi
log "no match user=$user rc=$rc"
exit 1
