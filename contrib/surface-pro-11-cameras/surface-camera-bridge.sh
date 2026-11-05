#!/bin/bash
# Surface Pro 11 Camera Bridge
# Pipes icamerasrc output to v4l2loopback virtual devices for app compatibility.
#
# Usage:
#   surface-camera-bridge.sh imx681-uf     # Start front camera bridge (runs until killed)
#   surface-camera-bridge.sh ov13858-uf    # Start rear camera bridge
#
# Normally started by the surface-camera-bridge@.service systemd user service.

set -euo pipefail

CAMERA="${1:-imx681-uf}"
GST_PLUGIN_PATH="${GST_PLUGIN_PATH:-/usr/lib/gstreamer-1.0}"
export GST_PLUGIN_PATH

# Map camera name to v4l2loopback device
case "$CAMERA" in
    imx681-uf)  V4L2_DEV="/dev/video50" ;;
    ov13858-uf) V4L2_DEV="/dev/video51" ;;
    *)
        echo "Unknown camera: $CAMERA (expected imx681-uf or ov13858-uf)" >&2
        exit 1
        ;;
esac

if [ ! -e "$V4L2_DEV" ]; then
    echo "v4l2loopback device $V4L2_DEV not found. Is v4l2loopback loaded?" >&2
    echo "  sudo modprobe v4l2loopback" >&2
    exit 1
fi

# The IPU7 ISYS capture devices have their ACLs stripped (by udev rules) to hide
# them from sandboxed apps like Firefox. The camera HAL needs access to them, so
# re-grant access for the current user before starting the pipeline.
sudo /usr/local/bin/surface-camera-isys-grant.sh "$(id -un)"

# Clean stale camera locks
rm -f /dev/shm/sem.camlock
ipcrm -m $(ipcs -m | grep 0x0043414d | awk '{print $2}') 2>/dev/null || true

# PipeWire/WirePlumber caches v4l2loopback as output-only if it enumerates
# before the bridge opens the device. Restart WirePlumber after a delay
# (in the background) so the device appears as a Video/Source for apps.
(sleep 5 && systemctl --user restart wireplumber 2>/dev/null || true) &

exec gst-launch-1.0 -e \
    icamerasrc device-name="$CAMERA" \
    ! "video/x-raw,format=NV12,width=1920,height=1080" \
    ! videoconvert \
    ! "video/x-raw,format=YUY2" \
    ! v4l2sink device="$V4L2_DEV" sync=false
