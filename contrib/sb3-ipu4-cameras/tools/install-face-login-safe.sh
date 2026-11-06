#!/bin/bash
# Universal face login: empty password field submits Howdy via PAM.
# No greeter button, no plasmalogin autologin, no evdev chord.
# FACE_LOGIN_ROOT=/mount bash this-script   applies onto another root.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${FACE_LOGIN_ROOT:-}"
if [[ -n "$DEST" ]]; then
  DEST="${DEST%/}"
fi

if [[ $(id -u) -ne 0 ]]; then
  echo "Run: sudo bash $0" >&2
  exit 1
fi

FACE_LINE='auth        [success=done default=ignore]            pam_exec.so expose_authtok quiet /usr/local/sbin/face-pam-auth.sh'
CRED_LINE='auth        [success=done default=ignore]                pam_face_cred.so'

install -d "${DEST}/usr/local/sbin"
install -m 755 "$SRC/sbin/face-pam-auth.sh" "${DEST}/usr/local/sbin/face-pam-auth.sh"
install -m 755 "$SRC/sbin/face-viewer" "${DEST}/usr/local/sbin/face-viewer"
if [[ -f "$SRC/sbin/face-resident.py" ]]; then
  install -m 755 "$SRC/sbin/face-resident.py" "${DEST}/usr/local/sbin/face-resident.py"
fi
if [[ -f "$SRC/tmpfiles.d/face-login.conf" ]]; then
  install -d "${DEST}/etc/tmpfiles.d"
  install -m 644 "$SRC/tmpfiles.d/face-login.conf" "${DEST}/etc/tmpfiles.d/face-login.conf"
fi
if [[ -f "$SRC/systemd/surface-face-resident.service" ]]; then
  install -d "${DEST}/etc/systemd/system"
  install -m 644 "$SRC/systemd/surface-face-resident.service" \
    "${DEST}/etc/systemd/system/surface-face-resident.service"
fi

# pam_face_cred.so — setcred success after empty-password face match
if [[ -f "$SRC/pam/pam_face_cred.c" ]]; then
  secdir=""
  for d in /usr/lib64/security /usr/lib/security /lib64/security /lib/security; do
    if [[ -d "${DEST}${d}" ]] || { [[ -z "$DEST" ]] && [[ -d "$d" ]]; }; then
      secdir="${DEST}${d}"
      break
    fi
  done
  if [[ -z "$secdir" ]]; then
    secdir="${DEST}/usr/lib64/security"
    install -d "$secdir"
  fi
  tmp_so="$(mktemp /tmp/pam_face_cred.XXXXXX.so)"
  if gcc -shared -fPIC -O2 -o "$tmp_so" "$SRC/pam/pam_face_cred.c" -lpam 2>/dev/null; then
    install -m 755 "$tmp_so" "$secdir/pam_face_cred.so"
    echo "pam_face_cred: $secdir/pam_face_cred.so"
  else
    echo "pam_face_cred: build skipped (gcc/libpam missing)" >&2
  fi
  rm -f "$tmp_so"
fi
# X11-only Plasma helpers abort without DISPLAY and delay the desktop
# after a face match. Scope DISPLAY=:0 to those units only.
if [[ -d "$SRC/systemd/user" ]]; then
  while IFS= read -r -d '' conf; do
    rel="${conf#"$SRC/systemd/user/"}"
    install -d "${DEST}/etc/systemd/user/$(dirname "$rel")"
    install -m 644 "$conf" "${DEST}/etc/systemd/user/$rel"
  done < <(find "$SRC/systemd/user" -name 'display-after-kwin.conf' -print0 2>/dev/null)
fi

if [[ -z "$DEST" ]]; then
  restorecon -F /usr/local/sbin/face-pam-auth.sh /usr/local/sbin/face-viewer 2>/dev/null || \
    chcon -t bin_t /usr/local/sbin/face-pam-auth.sh /usr/local/sbin/face-viewer 2>/dev/null || true
  systemd-tmpfiles --create /etc/tmpfiles.d/face-login.conf 2>/dev/null || true
  systemctl daemon-reload 2>/dev/null || true
  systemctl enable --now surface-face-resident.service 2>/dev/null || true
  systemctl --global daemon-reload 2>/dev/null || true
else
  chcon -t bin_t "${DEST}/usr/local/sbin/face-pam-auth.sh" \
    "${DEST}/usr/local/sbin/face-viewer" 2>/dev/null || true
fi

insert_face_line() {
  local f="$1"
  [[ -f "$f" ]] || return 0
  python3 - "$f" "$FACE_LINE" "$CRED_LINE" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
line = sys.argv[2]
cred = sys.argv[3]
text = p.read_text()
if "face-pam-auth.sh" in text:
    if "pam_face_cred.so" not in text:
        text = text.replace(line, line + "\n" + cred, 1)
        p.write_text(text if text.endswith("\n") else text + "\n")
        print("cred:", p)
    raise SystemExit(0)
out = []
done = False
for raw in text.splitlines():
    if (not done) and raw.strip().startswith("auth") and "pam_deny.so" in raw:
        out.append(line)
        out.append(cred)
        done = True
    out.append(raw)
if not done:
    raise SystemExit(0)
p.write_text("\n".join(out) + "\n")
print("pam:", p)
PY
}

for f in \
  "${DEST}/etc/pam.d/password-auth" \
  "${DEST}/etc/pam.d/system-auth" \
  "${DEST}/etc/pam.d/common-auth" \
  "${DEST}/etc/authselect/password-auth" \
  "${DEST}/etc/authselect/system-auth"
do
  insert_face_line "$f"
done

if [[ -d "${DEST}/usr/share/pam-configs" ]]; then
  cat >"${DEST}/usr/share/pam-configs/face-login" <<'CFG'
Name: Face login when the password field is empty
Default: no
Priority: 128
Auth-Type: Additional
Auth:
	[success=done default=ignore] pam_exec.so expose_authtok quiet /usr/local/sbin/face-pam-auth.sh
CFG
fi

sysroot_ctl() {
  if [[ -n "$DEST" ]]; then
    systemctl --root="$DEST" "$@"
  else
    systemctl "$@"
  fi
}

# Drop the button path before masking, while the unit files still exist.
sysroot_ctl disable surface-face-greeter-button.service 2>/dev/null || true
sysroot_ctl disable surface-face-login.service 2>/dev/null || true
sysroot_ctl disable surface-face-login.path 2>/dev/null || true

cap_timeout() {
  local f="$1" sec="$2"
  [[ -f "$f" && ! -L "$f" ]] || return 0
  python3 - "$f" "$sec" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
sec = int(sys.argv[2])
text = p.read_text()
out = []
changed = False
for raw in text.splitlines():
    if raw.startswith("TimeoutStartSec=") or raw.startswith("TimeoutStopSec="):
        key, val = raw.split("=", 1)
        try:
            cur = int(val.strip())
        except ValueError:
            out.append(raw)
            continue
        if cur > sec:
            raw = f"{key}={sec}"
            changed = True
    if raw.startswith("Restart=on-failure") and p.name == "surface-face-greeter-button.service":
        raw = "Restart=no"
        changed = True
    out.append(raw)
if changed:
    p.write_text("\n".join(out) + "\n")
    print("timeout:", p)
PY
}

for f in \
  "${DEST}/etc/systemd/system/surface-ipu4-late.service" \
  "${DEST}/etc/systemd/system/sb3-ipu4-late.service" \
  "${DEST}/etc/systemd/system/surface-nvidia-retry.service" \
  "${DEST}/etc/systemd/system/surface-nvidia-late.service" \
  "${DEST}/etc/systemd/system/surface-howdy-test.service" \
  "${DEST}/etc/systemd/system/surface-ir-howdy.service" \
  "${DEST}/etc/systemd/system/surface-face-login.service" \
  "${DEST}/etc/systemd/system/surface-webcam.service" \
  "${DEST}/etc/systemd/system/sb3-webcam.service" \
  "$SRC/systemd/surface-ipu4-late.service" \
  "$SRC/systemd/surface-nvidia-retry.service" \
  "$SRC/systemd/surface-ir-howdy.service" \
  "$SRC/systemd/surface-face-login.service" \
  "$SRC/systemd/surface-face-greeter-button.service" \
  "$SRC/systemd/surface-webcam.service" \
  "$SRC/surface-ipu4-late.service"
do
  cap_timeout "$f" 10
done

# Repo button unit: no restart loop. Disk copy is masked below.
if [[ -f "$SRC/systemd/surface-face-greeter-button.service" ]]; then
  sed -i 's/^Restart=on-failure$/Restart=no/' "$SRC/systemd/surface-face-greeter-button.service" || true
fi
if [[ -f "$SRC/systemd/surface-face-login.path" ]]; then
  sed -i '/^After=plasmalogin/d' "$SRC/systemd/surface-face-login.path" || true
fi

# Drop only the greeter token. Deleting the whole After= line removed
# local-fs and modules-load with it. Late init also must not wait for
# the display manager: webcam is WantedBy=multi-user and After=late, so
# late After=display-manager cycled the greeter.
strip_greeter_order() {
  local unit="$1"
  [[ -f "$unit" && ! -L "$unit" ]] || return 0
  python3 - "$unit" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
out = []
changed = False
drop = {"plasmalogin.service", "display-manager.service"}
for raw in p.read_text().splitlines():
    if raw.startswith("After=") or raw.startswith("Wants="):
        key, vals = raw.split("=", 1)
        kept = [v for v in vals.split() if v not in drop]
        new = f"{key}={' '.join(kept)}" if kept else None
        if new != raw:
            changed = True
        if new:
            out.append(new)
        continue
    out.append(raw)
if changed:
    p.write_text("\n".join(out) + "\n")
    print("order:", p)
PY
}
strip_greeter_order "${DEST}/etc/systemd/system/sb3-ipu4-late.service"
strip_greeter_order "${DEST}/etc/systemd/system/surface-ipu4-late.service"
strip_greeter_order "$SRC/systemd/surface-ipu4-late.service"
strip_greeter_order "$SRC/surface-ipu4-late.service"
# graphical.target already wants this unit. A multi-user want plus
# After=display-manager is the same class of cycle.
rm -f "${DEST}/etc/systemd/system/multi-user.target.wants/surface-nvidia-late.service"

# Howdy scan limit stays under the 10s PAM kill.
if [[ -f "${DEST}/etc/howdy/config.ini" ]]; then
  python3 - "${DEST}/etc/howdy/config.ini" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
lines = []
changed = False
for raw in p.read_text().splitlines():
    stripped = raw.strip()
    if stripped.startswith("timeout"):
        parts = raw.split("=", 1)
        if len(parts) == 2:
            try:
                cur = int(parts[1].strip())
            except ValueError:
                lines.append(raw)
                continue
            if cur > 8:
                raw = parts[0].rstrip() + "= 8"
                changed = True
    lines.append(raw)
if changed:
    p.write_text("\n".join(lines) + "\n")
    print("howdy timeout:", p)
PY
fi

# Warm hold used to sit for 180s. Cap the repo and the installed copy.
for warm in "$SRC/sbin/surface-howdy-warm.sh" "${DEST}/usr/local/sbin/surface-howdy-warm.sh"; do
  [[ -f "$warm" ]] || continue
  sed -i 's/time.time() + 180/time.time() + 10/' "$warm"
done

# Late init modprobe waits of 45/60/90s become 8s so a stuck module
# cannot outlive the 10s unit timeout.
for late in "$SRC/sbin/surface-ipu4-late.sh" "${DEST}/usr/local/sbin/surface-ipu4-late.sh"; do
  [[ -f "$late" ]] || continue
  sed -i \
    -e 's/timeout -k 5 45 /timeout -k 1 8 /' \
    -e 's/timeout -k 5 90 /timeout -k 1 8 /' \
    -e 's/timeout -k 5 60 /timeout -k 1 8 /' \
    -e 's/timeout -k 2 12 /timeout -k 1 8 /' \
    "$late"
done

# Mask the greeter button and its path so boot cannot start them.
ln -sfn /dev/null "${DEST}/etc/systemd/system/surface-face-greeter-button.service"
ln -sfn /dev/null "${DEST}/etc/systemd/system/surface-face-login.path"
rm -f "${DEST}/etc/systemd/system/graphical.target.wants/surface-face-greeter-button.service"
rm -f "${DEST}/etc/systemd/system/multi-user.target.wants/surface-face-login.path"
rm -f "${DEST}/etc/systemd/system/multi-user.target.wants/surface-face-login.service"


# Keep the line across authselect apply-changes.
prof="${DEST}/etc/authselect/custom/face-login"
if [[ -d "${DEST}/usr/share/authselect/default/local" ]]; then
  rm -rf "$prof"
  cp -a "${DEST}/usr/share/authselect/default/local" "$prof"
  insert_face_line "$prof/password-auth"
  insert_face_line "$prof/system-auth"
  conf="${DEST}/etc/authselect/authselect.conf"
  if [[ -f "$conf" ]]; then
    python3 - "$conf" <<'END'
import sys
from pathlib import Path
c = Path(sys.argv[1])
lines = c.read_text().splitlines()
if lines:
    lines[0] = "custom/face-login"
    c.write_text("\n".join(lines) + "\n")
END
  fi
fi

bash -n "${DEST}/usr/local/sbin/face-pam-auth.sh"
echo "OK: empty-password Howdy via PAM (no greeter button)"
