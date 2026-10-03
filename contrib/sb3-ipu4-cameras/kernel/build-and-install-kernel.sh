#!/usr/bin/env bash
# Vanilla 6.19.8 + linux-surface 6.19 + ruslanbay/ipu4-drivers + SB3 DPHY lock.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${SRC_ROOT:-/usr/src/surface-ipu4}"
KVER="${KVER:-6.19.8}"
JOBS="${JOBS:-$(nproc)}"
PATCH="$ROOT/kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch"

mkdir -p "$SRC_ROOT/logs"
cd "$SRC_ROOT"

need() { command -v "$1" >/dev/null || { echo "missing $1" >&2; exit 1; }; }
need git
need make
need curl
need tar

if [[ ! -d "$SRC_ROOT/ipu4-drivers/.git" ]]; then
  git clone --depth=1 -b main https://github.com/ruslanbay/ipu4-drivers.git
  (cd ipu4-drivers && git lfs pull)
fi
if [[ ! -d "$SRC_ROOT/linux-surface/.git" ]]; then
  git clone --depth=1 --filter=blob:none --sparse https://github.com/linux-surface/linux-surface.git
  (cd linux-surface && git sparse-checkout set patches)
fi
if [[ ! -d "$SRC_ROOT/linux-$KVER" ]]; then
  if [[ ! -f linux-$KVER.tar.xz ]]; then
    curl -fL --retry 3 -o "linux-$KVER.tar.xz" \
      "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KVER.tar.xz"
  fi
  tar -xf "linux-$KVER.tar.xz"
fi

cd "$SRC_ROOT/linux-$KVER"
if [[ ! -d .git ]]; then
  git init -q
  git -c user.email="surface-ipu4@localhost" -c user.name="surface-ipu4" add -A
  git -c user.email="surface-ipu4@localhost" -c user.name="surface-ipu4" commit -qm "v$KVER vanilla"
  git -c user.email="surface-ipu4@localhost" -c user.name="surface-ipu4" \
    am --3way "$SRC_ROOT/linux-surface/patches/6.19/"*.patch
  git -c user.email="surface-ipu4@localhost" -c user.name="surface-ipu4" \
    am --3way "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19/"*.patch
  git -c user.email="surface-ipu4@localhost" -c user.name="surface-ipu4" \
    am --3way "$PATCH"
fi

BOOTCFG=""
for c in /boot/config-$(uname -r) /lib/modules/$(uname -r)/config; do
  [[ -f "$c" ]] && BOOTCFG="$c" && break
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

echo "Building kernel. Log: $SRC_ROOT/logs/kernel-build.log"
make -j"$JOBS" 2>&1 | tee "$SRC_ROOT/logs/kernel-build.log"
REL="$(cat include/config/kernel.release)"
echo "Installing $REL"
"$ROOT/kernel/install-firmware.sh"
make modules_install INSTALL_MOD_STRIP=1
make install
echo "Kernel $REL installed. Reboot and select surface-ipu4."
