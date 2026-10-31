#!/usr/bin/env bash
# Build Surface Book 3 IPU4P camera kernel.
#
# Supported bases (set KVER, or pass as first argument):
#   6.19.8  — linux-surface patches/6.19 + ruslanbay downstream-legacy v6.19 (LTS optional)
#   7.2.12  — linux-surface patches/7.2  + same IPU4 v6.19 series + SB3 shims (DEFAULT)
#   7.3.0   — linux-surface/kernel v7.3-surface @ 1ea399d (+ tip on v7.3)
#             + Sakari IPU6/IPU7 lore + ruslanbay upstream 00*.patch
#             + SB3 overlays + loafecho PM_QOS/stop_streaming (#1353)
#
# Resulting LOCALVERSION: -surface-ipu4  (release e.g. 7.2.12-surface-ipu4+)
# Distros: Fedora 44/45, Nobara 44.
#
# Paths: SRC_ROOT / SURFACE_IPU4_BUILD only — never hard-code a username.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${SRC_ROOT:-${SURFACE_IPU4_BUILD:-/usr/src/surface-ipu4}}"
if [[ ! -w "${SRC_ROOT}" ]] 2>/dev/null; then
  SRC_ROOT="${SURFACE_IPU4_BUILD:-${HOME}/src/surface-ipu4-build}"
fi
KVER="${1:-${KVER:-${SURFACE_IPU4_KVER:-7.2.12}}}"
JOBS="${JOBS:-$(nproc)}"
IPU_TRACK="${IPU_TRACK:-}"   # auto | downstream | upstream
INSTALL_MODE="${INSTALL_MODE:-rpm}"  # rpm | modules  (7.3 default: rpm)

SURFACE_MODE="patches"   # patches | rebase-kernel
KERNEL_TAR_VER=""
case "$KVER" in
  6.19.*)
    SURFACE_SERIES="6.19"
    KERNEL_TAR_VER="$KVER"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_TAR_VER}.tar.xz"
    IPU_TRACK="${IPU_TRACK:-downstream}"
    ;;
  7.2.*)
    SURFACE_SERIES="7.2"
    KERNEL_TAR_VER="$KVER"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_TAR_VER}.tar.xz"
    IPU_TRACK="${IPU_TRACK:-downstream}"
    ;;
  7.3|7.3.0)
    SURFACE_SERIES="7.3"
    KERNEL_TAR_VER="7.3"
    KVER="7.3.0"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_TAR_VER}.tar.xz"
    SURFACE_MODE="rebase-kernel"
    IPU_TRACK="${IPU_TRACK:-upstream}"
    ;;
  7.3.*)
    SURFACE_SERIES="7.3"
    KERNEL_TAR_VER="$KVER"
    KERNEL_XZ_URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_TAR_VER}.tar.xz"
    SURFACE_MODE="rebase-kernel"
    IPU_TRACK="${IPU_TRACK:-upstream}"
    ;;
  *)
    echo "Unsupported KVER=$KVER (use 6.19.8, 7.2.12, or 7.3.0)" >&2
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
if [[ "$IPU_TRACK" == "upstream" ]]; then
  need b4
fi

echo "=== SB3 IPU4 kernel build: KVER=$KVER SURFACE=$SURFACE_SERIES MODE=$SURFACE_MODE IPU=$IPU_TRACK SRC=$SRC_ROOT ==="

# --- ipu4-drivers ---
if [[ ! -d "$SRC_ROOT/ipu4-drivers/.git" ]]; then
  git clone https://github.com/ruslanbay/ipu4-drivers.git
fi
(
  cd ipu4-drivers
  git fetch origin
  if [[ "$IPU_TRACK" == "upstream" ]]; then
    git checkout upstream
    git pull --ff-only origin upstream || true
  else
    git checkout downstream-legacy
    git pull --ff-only origin downstream-legacy || true
  fi
  git lfs pull || true
)

if [[ "$IPU_TRACK" == "downstream" ]]; then
  if [[ ! -d "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19" ]]; then
    echo "Missing ipu4-drivers/patches/kernel/v6.19 (downstream-legacy)" >&2
    exit 1
  fi
else
  if ! compgen -G "$SRC_ROOT/ipu4-drivers/patches/00*.patch" >/dev/null; then
    echo "Missing ipu4-drivers/patches/00*.patch (upstream branch)" >&2
    exit 1
  fi
fi

# Firmware blob lives on downstream-legacy; ensure it exists for install.
if [[ ! -f "$SRC_ROOT/ipu4-drivers/firmware/ipu4-20191030.bin" ]]; then
  git -C ipu4-drivers checkout downstream-legacy -- firmware/ipu4-20191030.bin 2>/dev/null || true
fi

LINUX_DIR="$SRC_ROOT/linux-${KERNEL_TAR_VER}"
if [[ "$SURFACE_MODE" == "rebase-kernel" ]]; then
  KERNTREE="$SRC_ROOT/linux-surface-kernel"
  if [[ ! -d "$KERNTREE/.git" ]]; then
    git clone --filter=blob:none --branch v7.3-surface --single-branch \
      https://github.com/linux-surface/kernel.git "$KERNTREE"
  else
    (cd "$KERNTREE" && git fetch origin v7.3-surface:v7.3-surface &&
       git fetch --deepen=800 origin v7.3-surface || true)
  fi
  (
    cd "$KERNTREE"
    git remote add torvalds https://github.com/torvalds/linux.git 2>/dev/null || true
    git fetch --depth=1 torvalds tag "v${KERNEL_TAR_VER}"
    git rebase --abort 2>/dev/null || true
    # Prefer linux-surface/kernel v7.3-surface tip (7.3.0-rc7). Override with
    # SURFACE_TIP=...  Discussion #1353 / loafecho tested at 1ea399d.
    DEFAULT_SURFACE_TIP="1ea399d60942"
    TIP="${SURFACE_TIP:-}"
    if [[ -z "$TIP" ]] && git cat-file -e "${DEFAULT_SURFACE_TIP}^{commit}" 2>/dev/null; then
      TIP="$DEFAULT_SURFACE_TIP"
    fi
    if [[ -n "$TIP" ]]; then
      echo "Using surface-only tip $TIP (on v${KERNEL_TAR_VER})"
      git checkout -B "build-${KVER}-upstream-surface" "$TIP"
      git reset --hard "$TIP"
    else
      git checkout -B "build-${KVER}-upstream-surface" v7.3-surface
      git reset --hard v7.3-surface
      BASE="$(timeout 60 git merge-base v7.3-surface "v${KERNEL_TAR_VER}" || true)"
      if [[ -z "$BASE" ]]; then
        echo "merge-base timed out — deepen history or set SURFACE_TIP=" >&2
        exit 1
      fi
      git rebase --onto "v${KERNEL_TAR_VER}" "$BASE" || {
        echo "Rebase of v7.3-surface onto v${KERNEL_TAR_VER} failed — resolve in $KERNTREE" >&2
        exit 1
      }
    fi
  )
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

MARKER=".sb3-ipu4-applied-${SURFACE_SERIES}-${IPU_TRACK}"
if [[ ! -f "$MARKER" ]]; then
  git am --abort 2>/dev/null || true

  if [[ "$SURFACE_MODE" == "patches" ]]; then
    git am --3way "$SRC_ROOT/linux-surface/patches/${SURFACE_SERIES}/"*.patch
  fi

  if [[ "$IPU_TRACK" == "upstream" ]]; then
    # Sakari Ailus IPU6→IPU7 series required by ruslanbay upstream README.
    b4 shazam 'https://lore.kernel.org/all/20260907113004.2489993-1-sakari.ailus@linux.intel.com/'
    git am --3way "$SRC_ROOT/ipu4-drivers/patches/"00*.patch
  else
    git am --3way "$SRC_ROOT/ipu4-drivers/patches/kernel/v6.19/"*.patch
    series_dphy="$ROOT/kernel/patches/${SURFACE_SERIES}/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch"
    if [[ -f "$series_dphy" ]]; then
      git am --3way "$series_dphy"
    else
      git am --3way "$ROOT/kernel/patches/0001-media-intel-ipu4p-sb3-front-dphy-lock.patch"
    fi
  fi

  # Series-local overlays (skip already-applied 0001 DPHY on downstream).
  extra="$ROOT/kernel/patches/${SURFACE_SERIES}"
  if [[ -d "$extra" ]]; then
    shopt -s nullglob
    for p in "$extra"/*.patch; do
      base="$(basename "$p")"
      if [[ "$IPU_TRACK" == "downstream" && "$base" == 0001-* ]]; then
        continue
      fi
      # Downstream-only API shims / dma_ops — skip on upstream IPU6 stack.
      if [[ "$IPU_TRACK" == "upstream" ]]; then
        case "$base" in
          0001-media-intel-ipu4p-sb3-front-dphy-lock.patch|\
          0002-uapi-ipu-*|\
          0003-media-ipu-buttress-*|\
          0004-media-ipu-isys-queue-*|\
          0005-media-ipu-isys-set-vbq-*|\
          0006-media-ipu-isys-avoid-*|\
          0008-media-intel-ipu-set-dma-ops-*)
            echo "SKIP downstream-only $base (upstream track)"
            continue
            ;;
        esac
      fi
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

# --- IPTS (touch + pen) must be present before .config ---
# v7.3-surface tip already carries drivers/hid/ipts. An older build path tried to
# re-apply linux-surface 0005-ipts on top; mei/pci-me.c rejected and some trees
# lost the Kconfig wire-up → olddefconfig dropped CONFIG_HID_IPTS → no /dev/hidraw
# for iptsd. Refuse to build a digitizer-less SB3 image.
ensure_ipts_sources() {
  if [[ -f drivers/hid/ipts/Kconfig && -f drivers/hid/ipts/main.c ]]; then
    if ! grep -q 'hid/ipts/Kconfig' drivers/hid/Kconfig; then
      echo 'source "drivers/hid/ipts/Kconfig"' >> drivers/hid/Kconfig
    fi
    if ! grep -q 'CONFIG_HID_IPTS' drivers/hid/Makefile; then
      echo 'obj-$(CONFIG_HID_IPTS)          += ipts/' >> drivers/hid/Makefile
    fi
    echo "OK IPTS sources present"
    return 0
  fi
  echo "IPTS sources missing — applying linux-surface 0005-ipts (resilient)" >&2
  local p=""
  for cand in \
    "$SRC_ROOT/linux-surface/patches/7.2/0005-ipts.patch" \
    "$SRC_ROOT/linux-surface/patches/6.19/0005-ipts.patch" \
    "$ROOT/../../patches/7.2/0005-ipts.patch" \
    "$ROOT/../../patches/6.19/0005-ipts.patch"
  do
    [[ -f "$cand" ]] && p="$cand" && break
  done
  if [[ -z "$p" ]]; then
    echo "FATAL: no 0005-ipts.patch found and drivers/hid/ipts/ missing" >&2
    exit 1
  fi
  # mei/pci-me.c often rejects on 7.3; driver files still apply. Keep going.
  git apply --reject --whitespace=nowarn "$p" || true
  rm -f drivers/misc/mei/pci-me.c.rej drivers/misc/mei/*.rej 2>/dev/null || true
  find drivers -name '*.rej' -delete 2>/dev/null || true
  if [[ ! -f drivers/hid/ipts/Kconfig || ! -f drivers/hid/ipts/main.c ]]; then
    echo "FATAL: 0005-ipts did not produce drivers/hid/ipts/" >&2
    exit 1
  fi
  if ! grep -q 'hid/ipts/Kconfig' drivers/hid/Kconfig; then
    echo 'source "drivers/hid/ipts/Kconfig"' >> drivers/hid/Kconfig
  fi
  if ! grep -q 'CONFIG_HID_IPTS' drivers/hid/Makefile; then
    echo 'obj-$(CONFIG_HID_IPTS)          += ipts/' >> drivers/hid/Makefile
  fi
  git add -A
  git commit -qm "SB3: ensure IPTS touch/pen driver (0005-ipts resilient)" || true
  echo "OK IPTS sources restored from $(basename "$p")"
}
ensure_ipts_sources

# --- config ---
# Never reuse a broken minimal .config. Prefer Fedora x86_64, then a known-good boot config.
BOOTCFG=""
if [[ "$IPU_TRACK" == "upstream" ]]; then
  FEDORA_CFG="$SRC_ROOT/kernel-x86_64-fedora.config"
  if [[ ! -f "$FEDORA_CFG" ]]; then
    curl -fL -o "$FEDORA_CFG" \
      'https://src.fedoraproject.org/rpms/kernel/raw/f44/f/kernel-x86_64-fedora.config'
  fi
  BOOTCFG="$FEDORA_CFG"
else
  for c in \
    "$ROOT/kernel/configs/surface-${KVER}-ipu4.config" \
    "$ROOT/kernel/configs/surface-${SURFACE_SERIES}.0-ipu4.config" \
    "$SRC_ROOT/linux-surface/configs/surface-${SURFACE_SERIES}.config" \
    "/boot/config-$(uname -r)" \
    /boot/config-*-surface*.x86_64
  do
    for f in $c; do
      [[ -f "$f" ]] && BOOTCFG="$f" && break 2
    done
  done
fi
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

# Boot-critical (previous 7.3 fail: these were unset → no root FS / NVMe).
./scripts/config --enable CONFIG_BTRFS_FS
./scripts/config --enable CONFIG_BTRFS_FS_POSIX_ACL || true
./scripts/config --module CONFIG_EXT4_FS || ./scripts/config --enable CONFIG_EXT4_FS
./scripts/config --module CONFIG_BLK_DEV_NVME || ./scripts/config --enable CONFIG_BLK_DEV_NVME
./scripts/config --module CONFIG_DRM_I915 || ./scripts/config --enable CONFIG_DRM_I915

./scripts/config --enable CONFIG_MEDIA_SUPPORT
./scripts/config --disable CONFIG_MEDIA_SUPPORT_FILTER || true
./scripts/config --enable CONFIG_MEDIA_CAMERA_SUPPORT
./scripts/config --enable CONFIG_MEDIA_PCI_SUPPORT
./scripts/config --module CONFIG_IPU_BRIDGE || ./scripts/config --enable CONFIG_IPU_BRIDGE
./scripts/config --enable CONFIG_INTEL_SKL_INT3472 || ./scripts/config --module CONFIG_INTEL_SKL_INT3472 || true
./scripts/config --module CONFIG_VIDEO_OV5693 || ./scripts/config --enable CONFIG_VIDEO_OV5693
./scripts/config --module CONFIG_VIDEO_OV7251 || ./scripts/config --enable CONFIG_VIDEO_OV7251
./scripts/config --module CONFIG_VIDEO_OV8865 || ./scripts/config --enable CONFIG_VIDEO_OV8865
./scripts/config --module CONFIG_VIDEO_OV9734 || true
# Surface Book touch + pen (IPTS) and newer THC HID.
# HID_IPTS depends on INTEL_MEI — enable MEI first or olddefconfig drops IPTS.
./scripts/config --module CONFIG_INTEL_MEI || ./scripts/config --enable CONFIG_INTEL_MEI
./scripts/config --module CONFIG_INTEL_MEI_ME || ./scripts/config --enable CONFIG_INTEL_MEI_ME
./scripts/config --module CONFIG_HID || ./scripts/config --enable CONFIG_HID
./scripts/config --module CONFIG_HID_IPTS || ./scripts/config --enable CONFIG_HID_IPTS
./scripts/config --module CONFIG_HID_ITHC || ./scripts/config --enable CONFIG_HID_ITHC
./scripts/config --module CONFIG_INTEL_THC_HID || ./scripts/config --enable CONFIG_INTEL_THC_HID
./scripts/config --module CONFIG_INTEL_QUICKSPI || true
./scripts/config --module CONFIG_INTEL_QUICKI2C || true

if [[ "$IPU_TRACK" == "upstream" ]]; then
  # ruslanbay upstream README (IPU4P on IPU6/IPU7)
  ./scripts/config --module CONFIG_VIDEO_INTEL_IPU6
  ./scripts/config --enable CONFIG_VIDEO_INTEL_IPU6_IPU7
  ./scripts/config --disable CONFIG_VIDEO_INTEL_IPU || true
  ./scripts/config --disable CONFIG_VIDEO_INTEL_IPU4 || true
  ./scripts/config --disable CONFIG_VIDEO_INTEL_IPU4P || true
else
  ./scripts/config --module CONFIG_VIDEO_INTEL_IPU || ./scripts/config --enable CONFIG_VIDEO_INTEL_IPU
  ./scripts/config --enable CONFIG_VIDEO_INTEL_IPU4P
  ./scripts/config --enable CONFIG_VIDEO_INTEL_IPU_SOC
  ./scripts/config --enable CONFIG_VIDEO_INTEL_IPU_FW_LIB
  ./scripts/config --disable CONFIG_VIDEO_INTEL_IPU6 || true
  ./scripts/config --disable CONFIG_VIDEO_INTEL_IPU7 || true
fi

./scripts/config --disable DEBUG_INFO
./scripts/config --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT
./scripts/config --disable DEBUG_INFO_BTF
./scripts/config --enable DEBUG_INFO_NONE
./scripts/config --disable CONFIG_DEBUG_INFO_BTF_MODULES || true
# Needed by some media debug helpers; cheap.
./scripts/config --enable CONFIG_DEBUG_FS || true
make olddefconfig

# Sanity: refuse to build an unbootable / digitizer-less config again.
# Prefer grepping .config — scripts/config --state can report "m" for a
# dangling symbol that olddefconfig already stripped when Kconfig was missing.
for req in CONFIG_BTRFS_FS CONFIG_BLK_DEV_NVME CONFIG_HID_IPTS CONFIG_INTEL_MEI; do
  if ! grep -qE "^${req}=[ym]" .config; then
    echo "FATAL: $req missing from .config after olddefconfig (got: $(./scripts/config --state "$req" 2>/dev/null || echo '?'))" >&2
    echo "FATAL: SB3 needs in-tree ipts.ko — see tools/rebuild-ipts-module.sh for OOT fallback only" >&2
    exit 1
  fi
  echo "OK $req=$(grep -E "^${req}=" .config)"
done
if [[ "$IPU_TRACK" == "upstream" ]]; then
  echo "OK VIDEO_INTEL_IPU6=$(./scripts/config --state CONFIG_VIDEO_INTEL_IPU6)"
  echo "OK VIDEO_INTEL_IPU6_IPU7=$(./scripts/config --state CONFIG_VIDEO_INTEL_IPU6_IPU7)"
fi

# Persist a known-good config snapshot into the overlay tree.
mkdir -p "$ROOT/kernel/configs"
cp -a .config "$ROOT/kernel/configs/surface-${KVER}-ipu4.config"

echo "Building kernel. Log: $SRC_ROOT/logs/kernel-build-${KVER}-${IPU_TRACK}.log"
"$ROOT/kernel/install-firmware.sh" || true

if [[ "$INSTALL_MODE" == "rpm" && "$IPU_TRACK" == "upstream" ]]; then
  make -j"$JOBS" binrpm-pkg 2>&1 | tee "$SRC_ROOT/logs/kernel-build-${KVER}-${IPU_TRACK}.log"
  REL="$(cat include/config/kernel.release)"
  echo "RPMs built for $REL — install with: dnf install $LINUX_DIR/rpmbuild/RPMS/x86_64/*.rpm"
  # Auto-install when we can elevate
  RPMDIR="$(pwd)/rpmbuild/RPMS/x86_64"
  if [[ -d "$RPMDIR" ]] && compgen -G "$RPMDIR"/*.rpm >/dev/null; then
    if command -v pkexec >/dev/null; then
      pkexec dnf install -y "$RPMDIR"/*.rpm
    else
      sudo dnf install -y "$RPMDIR"/*.rpm
    fi
  fi
else
  make -j"$JOBS" 2>&1 | tee "$SRC_ROOT/logs/kernel-build-${KVER}-${IPU_TRACK}.log"
  REL="$(cat include/config/kernel.release)"
  echo "Installing $REL"
  if command -v pkexec >/dev/null; then
    pkexec bash -c "cd '$LINUX_DIR' && make modules_install INSTALL_MOD_STRIP=1 && make install &&
      mkdir -p /usr/src/kernels &&
      ln -sfn '$LINUX_DIR' /usr/src/kernels/$REL &&
      ln -sfn '$LINUX_DIR' /lib/modules/$REL/build &&
      depmod -a '$REL'"
  else
    sudo make modules_install INSTALL_MOD_STRIP=1
    sudo make install
  fi
fi

REL="$(cat include/config/kernel.release)"
echo "Kernel $REL ready (IPU_TRACK=$IPU_TRACK). Reboot and select surface-ipu4."
echo "After reboot: journalctl -b | grep -Ei 'ipu|ov5693|ov8865|INT347'"
