#!/usr/bin/env bash
# Build Surface Book 3 IPU4P camera kernel.
#
# Supported bases (set KVER, or pass as first argument):
#   6.19.8  — linux-surface patches/6.19 + ruslanbay downstream-legacy v6.19
#   7.2.11  — linux-surface patches/7.2  + same IPU4 v6.19 series + SB3 shims
#   7.3.0   — upstream v7.3 + linux-surface v7.3-surface rebased onto v7.3
#             + same IPU4 v6.19 series + kernel/patches/7.3/
#
# Resulting LOCALVERSION: -surface-ipu4  (release e.g. 7.3.0-surface-ipu4+)
# Distros: Fedora 44/45, Nobara 44 (same RPM surface repo / custom install).
#
# Paths: SRC_ROOT / SURFACE_IPU4_BUILD only — never hard-code a username or
# /home/<user>/… tree.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${SRC_ROOT:-${SURFACE_IPU4_BUILD:-/usr/src/surface-ipu4}}"
# Prefer a writable home build tree when /usr/src is not owned by the builder.
if [[ ! -w "${SRC_ROOT}" ]] 2>/dev/null; then
  SRC_ROOT="${SURFACE_IPU4_BUILD:-${HOME}/src/surface-ipu4-build}"
fi
KVER="${1:-${KVER:-${SURFACE_IPU4_KVER:-7.3.0}}}"
JOBS="${JOBS:-$(nproc)}"
PATCH="$ROOT/kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch"

# Map packaging KVER → tarball name + surface series + how to get surface tree.
SURFACE_MODE="patches"   # patches | rebase-kernel
KERNEL_TAR_VER=""
case "$KVER" in
  6.19.*)
    SURFACE_SERIES="6.19"
    KERNEL_TAR_VER="$KVER"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_TAR_VER}.tar.xz"
    ;;
  7.2.*)
    SURFACE_SERIES="7.2"
    KERNEL_TAR_VER="$KVER"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_TAR_VER}.tar.xz"
    ;;
  7.3|7.3.0)
    # Upstream ships linux-7.3.tar.xz; Makefile reports VERSION 7.3.0.
    SURFACE_SERIES="7.3"
    KERNEL_TAR_VER="7.3"
    KVER="7.3.0"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_TAR_VER}.tar.xz"
    SURFACE_MODE="rebase-kernel"
    ;;
  7.3.*)
    SURFACE_SERIES="7.3"
    KERNEL_TAR_VER="$KVER"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_TAR_VER}.tar.xz"
    SURFACE_MODE="rebase-kernel"
    ;;
  *)
    echo "Unsupported KVER=$KVER (use 6.19.8, 7.2.11, or 7.3.0)" >&2
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

echo "=== SB3 IPU4 kernel build: KVER=$KVER SURFACE_SERIES=$SURFACE_SERIES MODE=$SURFACE_MODE SRC_ROOT=$SRC_ROOT ==="

# Downstream-legacy carries the intel_ipu4p stack used by surface_webcamd.
# Do not mix ruslanbay upstream (IPU6-based) into this path.
if [[ ! -d "$SRC_ROOT/ipu4-drivers/.git" ]]; then
  git clone -b downstream-legacy https://github.com/ruslanbay/ipu4-drivers.git
  (cd ipu4-drivers && git lfs pull)
else
  (cd ipu4-drivers && git fetch origin && git checkout downstream-legacy &&
     git pull --ff-only origin downstream-legacy && git lfs pull) || true
fi
if [[ ! -d "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19" ]]; then
  echo "Missing ipu4-drivers/patches/kernel/v6.19 (downstream-legacy)" >&2
  exit 1
fi

LINUX_DIR="$SRC_ROOT/linux-${KERNEL_TAR_VER}"
if [[ "$SURFACE_MODE" == "rebase-kernel" ]]; then
  # Full surface tree: rebase linux-surface/kernel v7.3-surface onto upstream v7.3.
  KERNTREE="$SRC_ROOT/linux-surface-kernel"
  if [[ ! -d "$KERNTREE/.git" ]]; then
    git clone --filter=blob:none --branch v7.3-surface --single-branch \
      https://github.com/linux-surface/kernel.git "$KERNTREE"
  else
    (cd "$KERNTREE" && git fetch origin v7.3-surface:v7.3-surface && git fetch --deepen=500 origin v7.3-surface || true)
  fi
  (cd "$KERNTREE" && git remote add torvalds https://github.com/torvalds/linux.git 2>/dev/null || true
   git fetch --depth=1 torvalds tag "v${KERNEL_TAR_VER}"
   git checkout -B "build-${KVER}-surface" v7.3-surface
   BASE="$(git merge-base v7.3-surface "v${KERNEL_TAR_VER}")"
   git rebase --onto "v${KERNEL_TAR_VER}" "$BASE" || {
     echo "Rebase of v7.3-surface onto v${KERNEL_TAR_VER} failed — resolve in $KERNTREE" >&2
     exit 1
   })
  LINUX_DIR="$KERNTREE"
else
  if [[ ! -d "$SRC_ROOT/linux-surface/.git" ]]; then
    git clone --depth=1 --filter=blob:none --sparse https://github.com/linux-surface/linux-surface.git
    (cd linux-surface && git sparse-checkout set patches configs)
  else
    (cd linux-surface && git fetch origin && git checkout origin/master -- "patches/${SURFACE_SERIES}" "configs" ||
       git pull --ff-only) || true
  fi
  if [[ ! -d "$SRC_ROOT/linux-surface/patches/${SURFACE_SERIES}" ]]; then
    echo "Missing linux-surface/patches/${SURFACE_SERIES}" >&2
    exit 1
  fi
  if [[ ! -d "$LINUX_DIR" ]]; then
    if [[ ! -f "linux-${KERNEL_TAR_VER}.tar.xz" ]]; then
      curl -fL --retry 3 -o "linux-${KERNEL_TAR_VER}.tar.xz" "$KERNEL_XZ_URL"
    fi
    tar -xf "linux-${KERNEL_TAR_VER}.tar.xz"
  fi
fi

cd "$LINUX_DIR"
git config user.email "surface-ipu4@localhost" 2>/dev/null || true
git config user.name "surface-ipu4" 2>/dev/null || true

if [[ ! -d .git ]]; then
  git init -q
  git add -A
  git commit -qm "v${KERNEL_TAR_VER} vanilla"
fi

MARKER=".sb3-ipu4-applied-${SURFACE_SERIES}"
if [[ ! -f "$MARKER" ]]; then
  git am --abort 2>/dev/null || true
  if [[ "$SURFACE_MODE" == "patches" ]]; then
    git am --3way "$SRC_ROOT/linux-surface/patches/${SURFACE_SERIES}/"*.patch
  fi
  git am --3way "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19/"*.patch
  # Prefer series-local DPHY; fall back to shared 0001.
  series_dphy="$ROOT/kernel/patches/${SURFACE_SERIES}/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch"
  if [[ -f "$series_dphy" ]]; then
    git am --3way "$series_dphy"
  else
    git am --3way "$PATCH"
  fi
  extra="$ROOT/kernel/patches/${SURFACE_SERIES}"
  if [[ -d "$extra" ]]; then
    shopt -s nullglob
    for p in "$extra"/*.patch; do
      base="$(basename "$p")"
      [[ "$base" == 0001-* ]] && continue
      if ! git am --3way "$p"; then
        git am --abort 2>/dev/null || true
        echo "WARN: git am failed for $base — try git apply" >&2
        git apply --whitespace=nowarn "$p"
        git add -A
        git commit -qm "$base"
      fi
    done
    shopt -u nullglob
  fi
  touch "$MARKER"
fi

BOOTCFG=""
for c in \
  "$ROOT/kernel/configs/surface-${KVER}-ipu4.config" \
  "$ROOT/kernel/configs/surface-${SURFACE_SERIES}.0-ipu4.config" \
  "$SRC_ROOT/linux-surface/configs/surface-${SURFACE_SERIES}.config" \
  "/lib/modules/$(uname -r)/config" \
  "/boot/config-$(uname -r)" \
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
./scripts/config --enable CONFIG_MEDIA_SUPPORT
./scripts/config --disable CONFIG_MEDIA_SUPPORT_FILTER || true
./scripts/config --enable CONFIG_MEDIA_CAMERA_SUPPORT
./scripts/config --enable CONFIG_MEDIA_PCI_SUPPORT
./scripts/config --module CONFIG_IPU_BRIDGE || ./scripts/config --enable CONFIG_IPU_BRIDGE
./scripts/config --module CONFIG_VIDEO_INTEL_IPU || ./scripts/config --enable CONFIG_VIDEO_INTEL_IPU
./scripts/config --enable CONFIG_VIDEO_INTEL_IPU4P
./scripts/config --enable CONFIG_VIDEO_INTEL_IPU_SOC
./scripts/config --enable CONFIG_VIDEO_INTEL_IPU_FW_LIB
./scripts/config --enable CONFIG_INTEL_SKL_INT3472 || ./scripts/config --module CONFIG_INTEL_SKL_INT3472 || true
./scripts/config --module CONFIG_VIDEO_OV5693 || ./scripts/config --enable CONFIG_VIDEO_OV5693
./scripts/config --module CONFIG_VIDEO_OV7251 || ./scripts/config --enable CONFIG_VIDEO_OV7251
./scripts/config --module CONFIG_VIDEO_OV8865 || ./scripts/config --enable CONFIG_VIDEO_OV8865
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
