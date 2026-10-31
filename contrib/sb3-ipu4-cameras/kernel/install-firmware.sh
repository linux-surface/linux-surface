#!/usr/bin/env bash
# IPU4P firmware — required before intel-ipu6 (upstream) / intel_ipu4p (legacy) can start.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${SRC_ROOT:-/usr/src/surface-ipu4}"
if [[ ! -f "$SRC_ROOT/ipu4-drivers/firmware/ipu4-20191030.bin" ]]; then
  for cand in "${HOME}/src/surface-ipu4-build" /usr/src/surface-ipu4; do
    [[ -f "$cand/ipu4-drivers/firmware/ipu4-20191030.bin" ]] && SRC_ROOT="$cand" && break
  done
fi
FW_SRC="$SRC_ROOT/ipu4-drivers/firmware/ipu4-20191030.bin"
# Downstream-legacy userspace/docs used ipu4p_cpd.bin; upstream IPU6 path uses intel/ipu/ipu4p_fw.bin
FW_DST_LEGACY="/usr/lib/firmware/ipu4p_cpd.bin"
FW_DST_UPSTREAM="/usr/lib/firmware/intel/ipu/ipu4p_fw.bin"
EXPECT_SHA="ff2c36cc81a5c726508b22970c2e2538ff06107dc5a72c93401403c227e5157f"

if [[ ! -f "$FW_SRC" ]]; then
  # Prefer blob from downstream-legacy even when building upstream track.
  if [[ -d "$SRC_ROOT/ipu4-drivers/.git" ]]; then
    git -C "$SRC_ROOT/ipu4-drivers" checkout downstream-legacy -- firmware/ipu4-20191030.bin 2>/dev/null || true
  fi
fi
if [[ ! -f "$FW_SRC" ]]; then
  echo "Missing $FW_SRC — clone ruslanbay/ipu4-drivers first (git lfs)." >&2
  exit 1
fi
got=$(sha256sum "$FW_SRC" | awk '{print $1}')
if [[ "$got" != "$EXPECT_SHA" ]]; then
  echo "Unexpected firmware SHA256: $got" >&2
  exit 1
fi

install_one() {
  local dst="$1"
  install -Dm644 "$FW_SRC" "$dst"
  restorecon -v "$dst" 2>/dev/null || true
  echo "firmware: $dst"
  sha256sum "$dst"
}

if [[ "$(id -u)" -eq 0 ]]; then
  install_one "$FW_DST_LEGACY"
  install_one "$FW_DST_UPSTREAM"
else
  # Caller should elevate; try pkexec for both paths.
  if command -v pkexec >/dev/null; then
    pkexec bash -c "install -Dm644 '$FW_SRC' '$FW_DST_LEGACY' &&
      install -Dm644 '$FW_SRC' '$FW_DST_UPSTREAM' &&
      restorecon -v '$FW_DST_LEGACY' '$FW_DST_UPSTREAM' 2>/dev/null || true &&
      sha256sum '$FW_DST_LEGACY' '$FW_DST_UPSTREAM'"
  else
    sudo bash -c "install -Dm644 '$FW_SRC' '$FW_DST_LEGACY' &&
      install -Dm644 '$FW_SRC' '$FW_DST_UPSTREAM'"
  fi
fi
