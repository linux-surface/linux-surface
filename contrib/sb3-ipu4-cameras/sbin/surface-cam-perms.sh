#!/bin/bash
# Raw IPU + IR: root-only, no logind ACL. Front/Back loopback: world video.
set -uo pipefail
setfacl_b() { setfacl -b "$1" 2>/dev/null || true; }

if [ -e /dev/media0 ]; then
  chown root:root /dev/media0 2>/dev/null || true
  chmod 0600 /dev/media0
  setfacl_b /dev/media0
fi

for sys in /sys/class/video4linux/video*; do
  [ -e "$sys/name" ] || continue
  name=$(cat "$sys/name" 2>/dev/null || true)
  dev="/dev/${sys##*/}"
  [ -e "$dev" ] || continue
  case "$name" in
    Surface-Front|Surface-Back)
      chown root:video "$dev" 2>/dev/null || true
      chmod 0666 "$dev"
      setfacl_b "$dev"
      ;;
    Surface-IR-Howdy)
      chown root:root "$dev" 2>/dev/null || true
      chmod 0600 "$dev"
      setfacl_b "$dev"
      ;;
    Intel\ IPU*|Intel-IPU*)
      chown root:root "$dev" 2>/dev/null || true
      chmod 0600 "$dev"
      setfacl_b "$dev"
      ;;
  esac
done
exit 0
