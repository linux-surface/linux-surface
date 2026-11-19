#!/bin/bash
# Re-run the SB3 dGPU wake when the clipboard/base is attached again.
# Boot-time load lives in surface-nvidia-late.sh; this covers hotplug.
set -uo pipefail
if lspci -n | grep -q '10de:'; then
  exit 0
fi
exec /usr/local/sbin/surface-nvidia-late.sh
