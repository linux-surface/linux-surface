#!/bin/bash
# Grant a user access to IPU7 ISYS capture devices.
# Called by the camera bridge script via sudo.
# The ISYS devices have their ACLs stripped by udev rules to hide them from
# sandboxed apps (Firefox snap, etc). This script re-grants access for the
# camera HAL which needs to program the ISYS hardware.

set -euo pipefail

USER="${1:?Usage: $0 USERNAME}"

# Validate username exists
if ! id "$USER" &>/dev/null; then
    echo "Unknown user: $USER" >&2
    exit 1
fi

for dev in /dev/video*; do
    name=$(cat "/sys/class/video4linux/$(basename "$dev")/name" 2>/dev/null || true)
    if [[ "$name" == "Intel IPU7 ISYS Capture"* ]]; then
        setfacl -m "u:${USER}:rw" "$dev" 2>/dev/null || true
    fi
done
