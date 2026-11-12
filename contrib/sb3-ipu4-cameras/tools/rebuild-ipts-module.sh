#!/usr/bin/env bash
# Out-of-tree IPTS fallback when an older surface-ipu4 image shipped without
# CONFIG_HID_IPTS. Prefer rebuilding the kernel (build-and-install-kernel.sh)
# so ipts.ko lands in the package; use this only until the next kernel rebuild.
set -euo pipefail
KVER="${1:-$(uname -r)}"
SRC_DIR="${IPTS_OOT_SRC:-/usr/local/src/ipts-oot}"
BUILD="/tmp/ipts-oot-build-$$"
if [[ ! -d "$SRC_DIR" ]]; then
  echo "Brak źródeł w $SRC_DIR (skopiuj drivers/hid/ipts z linux-surface)." >&2
  exit 1
fi
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp -a "$SRC_DIR"/*.c "$SRC_DIR"/*.h "$SRC_DIR"/Makefile "$BUILD"/
make -C "/lib/modules/${KVER}/build" M="$BUILD" modules
sudo mkdir -p "/lib/modules/${KVER}/extra/ipts"
sudo install -m 644 "$BUILD/ipts.ko" "/lib/modules/${KVER}/extra/ipts/ipts.ko"
echo ipts | sudo tee /etc/modules-load.d/ipts.conf >/dev/null
sudo depmod -a "$KVER"
sudo modprobe -r ipts 2>/dev/null || true
sudo modprobe ipts
echo "ipts zainstalowany dla $KVER (OOT fallback)"
lsmod | grep '^ipts'
