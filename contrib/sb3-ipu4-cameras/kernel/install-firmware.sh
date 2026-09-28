#!/usr/bin/env bash
# IPU4P (Intel Camera) firmware — required before intel_ipu4p can start.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${SRC_ROOT:-/usr/src/surface-ipu4}"
FW_SRC="$SRC_ROOT/ipu4-drivers/firmware/ipu4-20191030.bin"
FW_DST="/usr/lib/firmware/ipu4p_cpd.bin"
EXPECT_SHA="ff2c36cc81a5c726508b22970c2e2538ff06107dc5a72c93401403c227e5157f"

if [[ ! -f "$FW_SRC" ]]; then
  echo "Missing $FW_SRC — clone ruslanbay/ipu4-drivers first (git lfs)." >&2
  exit 1
fi
got=$(sha256sum "$FW_SRC" | awk '{print $1}')
if [[ "$got" != "$EXPECT_SHA" ]]; then
  echo "Unexpected firmware SHA256: $got" >&2
  exit 1
fi
install -Dm644 "$FW_SRC" "$FW_DST"
# SELinux (Fedora/RHEL); no-op elsewhere
restorecon -v "$FW_DST" 2>/dev/null || true
echo "firmware: $FW_DST"
sha256sum "$FW_DST"
