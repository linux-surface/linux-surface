#!/bin/bash
# Re-apply camera node permissions from /etc/udev/rules.d only.
# Never chown/chmod /dev here — see 90-surface-ipu4-cameras.rules and
# 99-surface-v4l2loopback.rules.
set -uo pipefail
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger -s video4linux -s media 2>/dev/null || true
exit 0
