#!/usr/bin/env bash
# Install Surface camera RGB profiles (7 loopbacks) onto the running system.
# Distro-neutral FHS paths only. Usage:
#   sudo ./apply-profiles-live.sh
#   sudo ./apply-profiles-live.sh /path/to/surface-cameras-production-ready
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PR="${1:-$ROOT}"
if [[ $(id -u) -ne 0 ]]; then
  echo "Run as root: sudo $0 [path-to-production-ready]" >&2
  exit 1
fi
[[ -d "$PR/userspace" ]] || { echo "missing $PR/userspace" >&2; exit 1; }

systemctl stop surface-webcam.service 2>/dev/null || true
sleep 0.5

PREFIX=/opt/surface-cameras
install -d "$PREFIX/tools" "$PREFIX/lib" "$PREFIX/sbin"
install -m 644 "$PR/userspace/lib/"*.py "$PREFIX/lib/"
install -m 755 "$PR/userspace/tools/surface_webcamd.py" "$PREFIX/tools/"
if [[ -f "$PR/userspace/tools/raw_hold.c" ]]; then
  install -m 644 "$PR/userspace/tools/raw_hold.c" "$PREFIX/tools/"
  cc -O2 -Wall -o "$PREFIX/tools/raw_hold" "$PREFIX/tools/raw_hold.c"
  chmod 755 "$PREFIX/tools/raw_hold"
fi
if [[ -d "$PR/userspace/sbin" ]]; then
  install -m 755 "$PR/userspace/sbin/"* "$PREFIX/sbin/" 2>/dev/null || true
fi

install -d /usr/local/sbin /usr/local/bin
install -m 755 "$PR/sbin/"*.sh /usr/local/sbin/
[[ -f "$PR/sbin/howdy-gtk-pam-safe" ]] && \
  install -m 755 "$PR/sbin/howdy-gtk-pam-safe" /usr/local/bin/howdy-gtk || true

install -m 644 "$PR/systemd/"*.service /etc/systemd/system/
install -m 644 "$PR/udev/"*.rules /etc/udev/rules.d/
install -d /etc/modprobe.d /etc/modules-load.d /etc/wireplumber/wireplumber.conf.d
install -m 644 "$PR/modprobe.d/"*.conf /etc/modprobe.d/
[[ -d "$PR/modules-load.d" ]] && install -m 644 "$PR/modules-load.d/"*.conf /etc/modules-load.d/ || true
install -m 644 "$PR/wireplumber/"*.conf /etc/wireplumber/wireplumber.conf.d/
# OBS 98-v4l2loopback.conf overwrites card labels — neutralize it
printf '%s\n' '# disabled — Surface cameras use 00/99-surface-v4l2loopback.conf' \
  >/etc/modprobe.d/98-v4l2loopback.conf || true

udevadm control --reload-rules 2>/dev/null || true
systemctl daemon-reload

fuser -k /dev/video60 /dev/video61 /dev/video62 /dev/video63 /dev/video64 /dev/video65 /dev/video66 2>/dev/null || true
sleep 0.3
modprobe -r v4l2loopback 2>/dev/null || true
sleep 0.8
modprobe v4l2loopback
sleep 0.8

echo "=== Surface loopbacks ==="
for n in /sys/class/video4linux/video*/name; do
  name=$(cat "$n" 2>/dev/null || true)
  case "$name" in Surface-*) echo "$(basename "$(dirname "$n")"): $name";; esac
done

/usr/local/sbin/surface-cam-perms.sh 2>/dev/null || true
/usr/local/sbin/configure-howdy-ir.sh 2>/dev/null || true
systemctl reset-failed surface-webcam.service 2>/dev/null || true
systemctl start surface-webcam.service
sleep 2
echo "active=$(systemctl is-active surface-webcam.service)"
cat /run/surface-webcam/status 2>/dev/null || true
journalctl -u surface-webcam.service -n 20 --no-pager 2>/dev/null || true
echo "DONE — pick Surface-Front-Standard in the browser (default, native 4:3)"
