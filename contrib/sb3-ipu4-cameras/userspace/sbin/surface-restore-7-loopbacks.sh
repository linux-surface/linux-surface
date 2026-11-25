#!/bin/bash
# Ensure 7 Surface v4l2loopback devices (never the OBS/3-cam fallback).
set -euo pipefail
CONF_OVERRIDE=/etc/modprobe.d/99-surface-v4l2loopback.conf
LIB_OBS=/usr/lib/modprobe.d/98-v4l2loopback.conf
OPTS='devices=7 video_nr=60,61,62,63,64,65,66 card_label=Surface-Front-Standard,Surface-Front-HQ,Surface-Front-Fast,Surface-Back-Standard,Surface-Back-HQ,Surface-Back-Fast,Surface-IR-Howdy exclusive_caps=1,1,1,1,1,1,1 max_buffers=8'

cat >"$CONF_OVERRIDE" <<EOF
# Overrides /usr/lib/modprobe.d/98-v4l2loopback.conf (OBS / 3-cam).
options v4l2loopback $OPTS
EOF

# Neutralize packaged 3-device OBS camera options (modprobe merges badly).
if [[ -f "$LIB_OBS" ]]; then
  mkdir -p /etc/modprobe.d
  # Mask by installing an empty same-basename under /etc (takes precedence on Fedora).
  cat >/etc/modprobe.d/98-v4l2loopback.conf <<'EOF'
# Masked by Surface 7-profile stack — see 99-surface-v4l2loopback.conf
# (original lived in /usr/lib/modprobe.d/98-v4l2loopback.conf with devices=3)
EOF
fi

systemctl stop surface-webcam.service 2>/dev/null || true
# Drop holders
for d in /dev/video6{0,1,2,3,4,5,6}; do
  fuser -k "$d" 2>/dev/null || true
done
sleep 0.3
if lsmod | grep -q '^v4l2loopback'; then
  rmmod v4l2loopback || { echo "rmmod failed — close camera apps"; exit 1; }
fi
modprobe v4l2loopback $OPTS
sleep 0.4
echo "=== loopbacks ==="
for n in 60 61 62 63 64 65 66; do
  if [[ -e /sys/class/video4linux/video$n/name ]]; then
    printf 'video%d: %s\n' "$n" "$(cat /sys/class/video4linux/video$n/name)"
  else
    echo "MISSING video$n"
  fi
done
/usr/local/sbin/surface-cam-perms.sh 2>/dev/null || true
# Point Howdy at IR loopback (video66 in 7-profile layout)
if [[ -x /usr/local/sbin/surface-howdy-gate.sh ]]; then
  /usr/local/sbin/surface-howdy-gate.sh --enable || true
elif [[ -x /usr/local/sbin/configure-howdy-ir.sh ]]; then
  /usr/local/sbin/configure-howdy-ir.sh || true
fi
udevadm control --reload || true
udevadm trigger -s video4linux || true
systemctl reset-failed surface-webcam.service 2>/dev/null || true
systemctl start surface-webcam.service
systemctl is-active surface-webcam.service
