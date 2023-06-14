#!/bin/bash
set -euo pipefail

# get list of surface kernels with timestamp
kernels=$(find /boot -maxdepth 1 -name "vmlinuz-*.surface.*" -printf '%B@\t%p\n')

# sort by timestamp
kernels=$(echo "${kernels}" | sort -n)

# get latest kernel (last line) and extract path
kernel=$(echo "${kernels}" | tail -n1 | cut -f2)

echo $kernel

# update GRUB config
grubby --set-default "${kernel}"

# update timestamp for rEFInd (ensure it's marked as latest across all kernels,
# not just surface ones)
touch "${kernel}"
