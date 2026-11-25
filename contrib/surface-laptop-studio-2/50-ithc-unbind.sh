#!/bin/sh
# Workaround for linux-surface/linux-surface#1014:
# Unbind ithc PCI functions before s2idle suspend and rebind after resume to
# avoid the longstanding race between ithc THC re-init and NVIDIA resume.
set -u

ITHC_PCI="0000:00:10.6 0000:00:10.7"

case "$1" in
  pre)
    for pci in $ITHC_PCI; do
      if [ -e "/sys/bus/pci/devices/$pci/driver" ]; then
        echo "$pci" > /sys/bus/pci/drivers/ithc/unbind 2>/dev/null || true
      fi
    done
    ;;
  post)
    for pci in $ITHC_PCI; do
      if [ ! -e "/sys/bus/pci/devices/$pci/driver" ]; then
        echo "$pci" > /sys/bus/pci/drivers/ithc/bind 2>/dev/null || true
      fi
    done
    ;;
esac
