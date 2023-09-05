#!/bin/bash
set -euo pipefail

# Get list of surface kernels with timestamp
KERNELS="$(
    find /boot -maxdepth 1 -name 'vmlinuz-*.surface.*' -print0 | xargs -0 -I '{}' \
        stat -c "%W %n" {}
)"

# Sort by timestamp
KERNELS="$(echo "${KERNELS}" | sort -n)"

# Get latest kernel (last line) and extract the path
VMLINUX="$(echo "${KERNELS}" | tail -n1 | cut -d' ' -f2)"

echo "${VMLINUX}"

# update GRUB config
grubby --set-default "${VMLINUX}"

# Update timestamp for rEFInd
# Ensure it's marked as latest across all kernels, not just surface ones
touch "${VMLINUX}"
