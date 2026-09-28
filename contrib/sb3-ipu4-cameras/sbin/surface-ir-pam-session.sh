#!/bin/bash
# PAM session hook for Surface IR — MUST NOT touch sensor I2C / kill loopbacks.
# Old version ran ir-led-on + systemctl stop + pkill video60 on every login and
# caused Remote I/O errors / black screens after password on greeter.
set -u
LOG=/var/log/surface-ir-pam.log
mkdir -p /run/surface-howdy
echo "$(date -Iseconds) PAM_TYPE=${PAM_TYPE:-?} USER=${PAM_USER:-?} SERVICE=${PAM_SERVICE:-?}" >>"$LOG" 2>/dev/null || true
case "${PAM_TYPE:-}" in
  open_session)
    rm -f /run/surface-howdy/want-auth 2>/dev/null || true
    # Marker only — webcamd owns LED/STREAMON. Never ir-led-on / pkill here.
    echo "open_session: markers only (no I2C, no pkill)" >>"$LOG" 2>/dev/null || true
    ;;
  close_session)
    rm -f /run/surface-howdy/disable 2>/dev/null || true
    echo "close_session: ok" >>"$LOG" 2>/dev/null || true
    ;;
esac
exit 0
