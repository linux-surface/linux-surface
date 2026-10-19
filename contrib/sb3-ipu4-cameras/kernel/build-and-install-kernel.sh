#!/usr/bin/env bash
# Build Surface Book 3 IPU4P camera kernel.
#
# Supported bases (set KVER, or pass as first argument):
#   6.19.8  — linux-surface patches/6.19 + ruslanbay downstream-legacy v6.19
#   7.2.11  — linux-surface patches/7.2  + same IPU4 v6.19 series + SB3 DPHY
#
# Resulting LOCALVERSION: -surface-ipu4
# Distros: Fedora 44/45, Nobara 44 (same RPM surface repo / custom install).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${SRC_ROOT:-/usr/src/surface-ipu4}"
# Prefer a writable home build tree when /usr/src is not owned by the user.
if [[ ! -w "${SRC_ROOT}" ]] 2>/dev/null; then
  SRC_ROOT="${HOME}/Pulpit/cursor/surface-ipu4-build"
fi
KVER="${1:-${KVER:-7.2.11}}"
JOBS="${JOBS:-$(nproc)}"
PATCH="$ROOT/kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch"

case "$KVER" in
  6.19.*)
    SURFACE_SERIES="6.19"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KVER}.tar.xz"
    ;;
  7.2.*)
    SURFACE_SERIES="7.2"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KVER}.tar.xz"
    ;;
  7.3.*)
    SURFACE_SERIES="7.3"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KVER}.tar.xz"
    echo "NOTE: 7.3 support lands when linux-surface ships patches/7.3;" >&2
    echo "      ruslanbay upstream (IPU6-based) is a separate track." >&2
    ;;
  *)
    echo "Unsupported KVER=$KVER (use 6.19.8 or 7.2.11)" >&2
    exit 1
    ;;
esac

mkdir -p "$SRC_ROOT/logs"
cd "$SRC_ROOT"

need() { command -v "$1" >/dev/null || { echo "missing $1" >&2; exit 1; }; }
need git
need make
need curl
need tar

echo "=== SB3 IPU4 kernel build: KVER=$KVER SURFACE_SERIES=$SURFACE_SERIES SRC_ROOT=$SRC_ROOT ==="

# Downstream-legacy carries the intel_ipu4p stack used by surface_webcamd.
if [[ ! -d "$SRC_ROOT/ipu4-drivers/.git" ]]; then
  git clone -b downstream-legacy https://github.com/ruslanbay/ipu4-drivers.git
  (cd ipu4-drivers && git lfs pull)
else
  (cd ipu4-drivers && git fetch origin && git checkout downstream-legacy &&
     git pull --ff-only origin downstream-legacy && git lfs pull) || true
fi
if [[ ! -d "$SRC_ROOT/linux-surface/.git" ]]; then
  git clone --depth=1 --filter=blob:none --sparse https://github.com/linux-surface/linux-surface.git
  (cd linux-surface && git sparse-checkout set patches)
else
  (cd linux-surface && git fetch origin && git checkout origin/master -- "patches/${SURFACE_SERIES}" ||
     git pull --ff-only) || true
fi

if [[ ! -d "$SRC_ROOT/linux-surface/patches/${SURFACE_SERIES}" ]]; then
  echo "Missing linux-surface/patches/${SURFACE_SERIES}" >&2
  exit 1
fi
if [[ ! -d "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19" ]]; then
  echo "Missing ipu4-drivers/patches/kernel/v6.19 (downstream-legacy)" >&2
  exit 1
fi
if [[ ! -f "$PATCH" ]]; then
  echo "Missing SB3 DPHY patch: $PATCH" >&2
  exit 1
fi

if [[ ! -d "$SRC_ROOT/linux-$KVER" ]]; then
  if [[ ! -f "linux-$KVER.tar.xz" ]]; then
    curl -fL --retry 3 -o "linux-$KVER.tar.xz" "$KERNEL_XZ_URL"
  fi
  tar -xf "linux-$KVER.tar.xz"
fi

cd "$SRC_ROOT/linux-$KVER"
git config user.email "surface-ipu4@localhost" 2>/dev/null || true
git config user.name "surface-ipu4" 2>/dev/null || true

if [[ ! -d .git ]]; then
  git init -q
  git add -A
  git commit -qm "v$KVER vanilla"
fi

# Marker: patches already applied for this series
MARKER=".sb3-ipu4-applied-${SURFACE_SERIES}"
if [[ ! -f "$MARKER" ]]; then
  git am --abort 2>/dev/null || true
  git am --3way "$SRC_ROOT/linux-surface/patches/${SURFACE_SERIES}/"*.patch
  git am --3way "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19/"*.patch
  git am --3way "$PATCH"
  touch "$MARKER"
fi

BOOTCFG=""
for c in \
  "/boot/config-$(uname -r)" \
  "/lib/modules/$(uname -r)/config" \
  /boot/config-*-surface*.x86_64
do
  # shellcheck disable=SC2086
  for f in $c; do
    [[ -f "$f" ]] && BOOTCFG="$f" && break 2
  done
done
if [[ -n "$BOOTCFG" ]]; then
  cp "$BOOTCFG" .config
else
  make defconfig
fi
./scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
./scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
./scripts/config --set-str CONFIG_MODULE_SIG_KEY ""
./scripts/config --disable CONFIG_MODULE_SIG_ALL
./scripts/config --set-str CONFIG_LOCALVERSION "-surface-ipu4"
./scripts/config --disable CONFIG_LOCALVERSION_AUTO
./scripts/config --module CONFIG_IPU_BRIDGE
./scripts/config --module CONFIG_VIDEO_INTEL_IPU
./scripts/config --enable CONFIG_VIDEO_INTEL_IPU4P
./scripts/config --enable CONFIG_VIDEO_INTEL_IPU_SOC
./scripts/config --enable CONFIG_VIDEO_INTEL_IPU_FW_LIB
./scripts/config --enable CONFIG_INTEL_SKL_INT3472
./scripts/config --module CONFIG_VIDEO_OV5693
./scripts/config --module CONFIG_VIDEO_OV7251
./scripts/config --module CONFIG_VIDEO_OV8865
./scripts/config --disable CONFIG_VIDEO_INTEL_IPU6 || true
./scripts/config --disable CONFIG_VIDEO_INTEL_IPU7 || true
./scripts/config --disable DEBUG_INFO
./scripts/config --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT
./scripts/config --disable DEBUG_INFO_BTF
./scripts/config --enable DEBUG_INFO_NONE
./scripts/config --disable CONFIG_DEBUG_INFO_BTF_MODULES || true
make olddefconfig

echo "Building kernel. Log: $SRC_ROOT/logs/kernel-build-${KVER}.log"
make -j"$JOBS" 2>&1 | tee "$SRC_ROOT/logs/kernel-build-${KVER}.log"
REL="$(cat include/config/kernel.release)"
echo "Installing $REL"
"$ROOT/kernel/install-firmware.sh"
sudo make modules_install INSTALL_MOD_STRIP=1
sudo make install
echo "Kernel $REL installed. Reboot and select surface-ipu4 (KVER=$KVER)."
