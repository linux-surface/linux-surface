#!/bin/bash
# Surface Pro 11 (Intel Lunar Lake) Camera Setup
#
# Builds and installs the full IPU7 camera pipeline:
#   1. ipu7-drivers      — Out-of-tree IPU7 kernel modules (cloned + built)
#   2. ipu7-camera-bins  — Intel proprietary firmware + libraries (cloned from GitHub)
#   3. ipu7-camera-hal   — Camera HAL with Surface Pro 11 patches (cloned + patched)
#   4. icamerasrc        — GStreamer source plugin (cloned from GitHub)
#   5. v4l2loopback      — Virtual /dev/video devices for standard app compatibility
#   6. AIQD persistence  — tmpfiles.d config for ISP calibration caching
#   7. AIQB tuning files — Extracted from Microsoft firmware, or upstream fallback
#
# Graph settings binaries (IMX681, OV13858) are included in the HAL patch
# and get installed automatically when the HAL is built.
#
# Prerequisites:
#   - Kernel with Surface Pro 11 camera patches applied (0018 + 0021)
#   - Kernel headers for the running kernel (/lib/modules/$(uname -r)/build)
#   - Build tools: build-essential cmake autoconf automake libtool pkg-config
#   - GStreamer dev: libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
#
# Usage:
#   sudo ./install.sh              # Full install (clone, build, install)
#   sudo ./install.sh --remove     # Uninstall (libraries, configs, firmware)
#   sudo ./install.sh --status     # Check installation status
#   sudo ./install.sh --dry-run    # Show what would be done without doing it
#   sudo ./install.sh --build-dir=/path  # Use custom build directory
#   sudo ./install.sh --firmware=/path/to/SurfacePro11*.msi  # Provide firmware MSI

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/surface-camera-build}"
HAL_INSTALL_PREFIX="/usr"

# Intel GitHub repos
DRIVERS_REPO="https://github.com/intel/ipu7-drivers.git"
BINS_REPO="https://github.com/intel/ipu7-camera-bins.git"
HAL_REPO="https://github.com/intel/ipu7-camera-hal.git"
ICAMERASRC_REPO="https://github.com/intel/icamerasrc.git"
ICAMERASRC_BRANCH="icamerasrc_slim_api"

# Microsoft firmware download page (direct links change with each update)
MS_FW_PAGE="https://www.microsoft.com/en-us/download/details.aspx?id=108013"

# AIQB file names expected in the firmware
AIQB_FILES=(
    "imx681_MSHW0580_LNL.aiqb"
    "OV13858_MSHW0581_LNL.aiqb"
)

# Fallback AIQB (upstream OV08X40: GRBG Bayer, has noise model, close resolution)
# Used when Microsoft firmware AIQBs are not available. Produces usable images
# but with inaccurate colors and non-optimal noise reduction since the color
# matrices, lens shading, and noise model are calibrated for a different sensor.
FALLBACK_AIQB="OV08X40_BBG802N3_LNL.aiqb"

# HAL config install path (CMAKE_INSTALL_FULL_SYSCONFDIR with --prefix=/usr resolves to /etc)
HAL_CONFIG_DIR="/etc/camera/ipu7x"

info()  { echo -e "\033[0;32m[INFO]\033[0m $*"; }
warn()  { echo -e "\033[1;33m[WARN]\033[0m $*"; }
error() { echo -e "\033[0;31m[ERROR]\033[0m $*"; }
dry()   { echo -e "\033[0;36m[DRY-RUN]\033[0m $*"; }

# Ask the user to confirm a step. Returns 0 (proceed) or 1 (skip).
confirm_step() {
    local prompt="$1"
    echo ""
    echo -n "  $prompt [Y/n] "
    local reply
    read -r reply
    [[ -z "$reply" || "$reply" =~ ^[Yy]$ ]]
}

# Find the best matching patch directory for the running kernel version.
# Returns the path (e.g., "v6.17.7") or empty if none found.
find_driver_patches() {
    local drivers_dir="$1"
    local kver
    kver=$(uname -r | grep -oP '^\d+\.\d+(\.\d+)?')

    # Exact match first (e.g., v6.17.7)
    if [ -d "$drivers_dir/patch/v${kver}/out-of-tree" ]; then
        echo "v${kver}"
        return
    fi

    # Try major.minor match (e.g., 6.17.7 → try v6.17.*)
    local major_minor
    major_minor=$(echo "$kver" | grep -oP '^\d+\.\d+')
    local best=""
    for d in "$drivers_dir"/patch/v${major_minor}*/out-of-tree; do
        [ -d "$d" ] || continue
        best=$(basename "$(dirname "$d")")
    done
    if [ -n "$best" ]; then
        echo "$best"
        return
    fi

    # Try range match (e.g., v6.10-v6.12 for kernel 6.11)
    for d in "$drivers_dir"/patch/v*-v*/out-of-tree; do
        [ -d "$d" ] || continue
        local range_dir
        range_dir=$(basename "$(dirname "$d")")
        local lo hi
        lo=$(echo "$range_dir" | grep -oP 'v\K[\d.]+(?=-)' )
        hi=$(echo "$range_dir" | grep -oP '-v\K[\d.]+')
        if [ -n "$lo" ] && [ -n "$hi" ]; then
            if printf '%s\n%s\n%s\n' "$lo" "$major_minor" "$hi" | sort -V | head -2 | tail -1 | grep -qF "$major_minor"; then
                echo "$range_dir"
                return
            fi
        fi
    done
}

# ─── Parse arguments ─────────────────────────────────────────────────────────

ACTION="install"
DRY_RUN=false
for arg in "$@"; do
    case "$arg" in
        --remove|--uninstall)  ACTION="remove" ;;
        --status)              ACTION="status" ;;
        --dry-run)             DRY_RUN=true ;;
        --build-dir=*)         BUILD_DIR="${arg#*=}" ;;
        --firmware=*)          MS_FW_LOCAL="${arg#*=}" ;;
        --skip-aiqb)           SKIP_AIQB=true ;;
        --help|-h)
            echo "Usage: sudo $0 [options]"
            echo ""
            echo "Options:"
            echo "  --remove          Uninstall camera components"
            echo "  --status          Check installation status"
            echo "  --dry-run         Show what would be done without making changes"
            echo "  --build-dir=PATH  Build directory (default: /tmp/surface-camera-build)"
            echo "  --firmware=PATH   Path to downloaded Surface firmware .msi"
            echo "  --skip-aiqb       Skip AIQB extraction, use upstream fallback instead"
            echo ""
            echo "The script will:"
            echo "  1. Build and install IPU7 out-of-tree kernel modules"
            echo "  2. Clone and install Intel IPU7 firmware + proprietary libraries"
            echo "  3. Clone, patch, and build the camera HAL (includes graph settings)"
            echo "  4. Build the GStreamer icamerasrc plugin"
            echo "  5. Set up v4l2loopback virtual cameras for app compatibility"
            echo "  6. Set up AIQD calibration persistence"
            echo "  7. Install AIQB tuning files (from Microsoft firmware or fallback)"
            exit 0
            ;;
        *) error "Unknown option: $arg"; exit 1 ;;
    esac
done

# ─── Status check ─────────────────────────────────────────────────────────────

check_status() {
    local all_ok=true

    echo "Surface Pro 11 Camera Installation Status"
    echo "=========================================="
    echo ""

    # Kernel modules
    echo "Kernel modules:"
    for mod in intel_ipu7 intel_ipu7_isys intel_ipu7_psys; do
        if lsmod | grep -q "^${mod}"; then
            echo "  [OK] $mod loaded"
        elif find "/lib/modules/$(uname -r)" -name "${mod//_/-}.ko*" 2>/dev/null | grep -q .; then
            echo "  [--] $mod installed but not loaded (reboot needed?)"
        else
            echo "  [--] $mod not loaded"
            all_ok=false
        fi
    done
    for mod in imx681 ov13858 vd55g0; do
        if lsmod | grep -q "^${mod}"; then
            echo "  [OK] $mod loaded"
        else
            echo "  [--] $mod not loaded"
        fi
    done
    echo ""

    # Firmware
    echo "Firmware:"
    if [ -f /lib/firmware/intel/ipu/ipu7x-fw.bin ]; then
        echo "  [OK] /lib/firmware/intel/ipu/ipu7x-fw.bin"
    else
        echo "  [!!] /lib/firmware/intel/ipu/ipu7x-fw.bin MISSING"
        all_ok=false
    fi
    echo ""

    # Libraries
    echo "Libraries:"
    for lib in libcamhal.so libcca.so libia_aic.so libia_lard.so; do
        if [ -f "/usr/lib/$lib" ]; then
            echo "  [OK] /usr/lib/$lib"
        else
            echo "  [!!] /usr/lib/$lib MISSING"
            all_ok=false
        fi
    done
    echo ""

    # HAL config
    echo "HAL configuration:"
    for f in libcamhal_configs.json sensors/imx681-uf.json sensors/ov13858-uf.json; do
        if [ -f "$HAL_CONFIG_DIR/$f" ]; then
            echo "  [OK] $f"
        else
            echo "  [!!] $f MISSING"
            all_ok=false
        fi
    done
    for f in "${AIQB_FILES[@]}"; do
        if [ -f "$HAL_CONFIG_DIR/$f" ]; then
            echo "  [OK] $f ($(du -h "$HAL_CONFIG_DIR/$f" | cut -f1))"
        else
            echo "  [!!] $f MISSING (camera will not produce good images)"
            all_ok=false
        fi
    done
    for f in gcss/IMX681_MSHW0580.IPU7X.bin gcss/OV13858_MSHW0580.IPU7X.bin; do
        if [ -f "$HAL_CONFIG_DIR/$f" ]; then
            echo "  [OK] $f"
        else
            echo "  [!!] $f MISSING"
            all_ok=false
        fi
    done
    echo ""

    # GStreamer plugin
    echo "GStreamer plugin:"
    if [ -f /usr/lib/gstreamer-1.0/libgsticamerasrc.so ]; then
        echo "  [OK] /usr/lib/gstreamer-1.0/libgsticamerasrc.so"
    else
        echo "  [!!] libgsticamerasrc.so MISSING"
        all_ok=false
    fi
    echo ""

    # AIQD persistence
    echo "AIQD persistence:"
    if [ -f /etc/tmpfiles.d/camera-hal.conf ]; then
        echo "  [OK] /etc/tmpfiles.d/camera-hal.conf"
    else
        echo "  [--] /etc/tmpfiles.d/camera-hal.conf not set up"
    fi
    echo ""

    # v4l2loopback
    echo "v4l2loopback virtual cameras:"
    if [ -f /etc/modprobe.d/v4l2loopback-surface.conf ]; then
        echo "  [OK] /etc/modprobe.d/v4l2loopback-surface.conf"
    else
        echo "  [--] modprobe config not set up"
    fi
    if lsmod | grep -q v4l2loopback; then
        echo "  [OK] v4l2loopback module loaded"
        [ -e /dev/video50 ] && echo "  [OK] /dev/video50 (front camera)" || echo "  [--] /dev/video50 not present"
        [ -e /dev/video51 ] && echo "  [OK] /dev/video51 (rear camera)" || echo "  [--] /dev/video51 not present"
    else
        echo "  [--] v4l2loopback module not loaded"
    fi
    if [ -f /usr/local/bin/surface-camera-bridge.sh ]; then
        echo "  [OK] Bridge script installed"
    else
        echo "  [--] Bridge script not installed"
    fi
    if [ -f /usr/local/bin/surface-camera-isys-grant.sh ]; then
        echo "  [OK] ISYS grant helper installed"
    else
        echo "  [--] ISYS grant helper not installed"
    fi
    if [ -f /etc/sudoers.d/surface-camera-isys-grant ]; then
        echo "  [OK] Sudoers rule installed"
    else
        echo "  [--] Sudoers rule not installed"
    fi
    if [ -f /etc/systemd/user/surface-camera-bridge@.service ]; then
        echo "  [OK] Systemd user service installed"
    else
        echo "  [--] Systemd user service not installed"
    fi
    echo ""

    # Video devices
    echo "Video devices:"
    if ls /dev/video* &>/dev/null; then
        for dev in /dev/video*; do
            name=$(cat "/sys/class/video4linux/$(basename "$dev")/name" 2>/dev/null || echo "unknown")
            echo "  $dev: $name"
        done
    else
        echo "  [!!] No video devices found"
    fi
    echo ""

    if $all_ok; then
        echo "Status: All components installed"
    else
        echo "Status: Some components missing (see [!!] above)"
    fi
}

# ─── Uninstall ────────────────────────────────────────────────────────────────

do_remove() {
    info "Removing Surface Pro 11 camera components..."

    # GStreamer plugin
    rm -f /usr/lib/gstreamer-1.0/libgsticamerasrc.so
    info "Removed GStreamer plugin"

    # HAL library
    rm -f /usr/lib/libcamhal.so*
    info "Removed camera HAL library"

    # HAL config (only Surface-specific files)
    rm -f "$HAL_CONFIG_DIR/sensors/imx681-uf.json"
    rm -f "$HAL_CONFIG_DIR/sensors/ov13858-uf.json"
    rm -f "$HAL_CONFIG_DIR/gcss/IMX681_MSHW0580.IPU7X.bin"
    rm -f "$HAL_CONFIG_DIR/gcss/OV13858_MSHW0580.IPU7X.bin"
    for f in "${AIQB_FILES[@]}"; do
        rm -f "$HAL_CONFIG_DIR/$f"
    done
    info "Removed HAL configuration files"

    # AIQD persistence
    rm -f /etc/tmpfiles.d/camera-hal.conf
    info "Removed AIQD tmpfiles config"

    # v4l2loopback config and services
    local real_user="${SUDO_USER:-$USER}"
    local real_uid
    real_uid=$(id -u "$real_user" 2>/dev/null || echo "")
    if [ -n "$real_uid" ]; then
        local _env=(sudo -u "$real_user" XDG_RUNTIME_DIR="/run/user/$real_uid"
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$real_uid/bus")
        "${_env[@]}" systemctl --user stop surface-camera-bridge@imx681-uf 2>/dev/null || true
        "${_env[@]}" systemctl --user stop surface-camera-bridge@ov13858-uf 2>/dev/null || true
        "${_env[@]}" systemctl --user disable surface-camera-bridge@imx681-uf 2>/dev/null || true
        "${_env[@]}" systemctl --user disable surface-camera-bridge@ov13858-uf 2>/dev/null || true
    fi
    rm -f /etc/modprobe.d/v4l2loopback-surface.conf
    rm -f /etc/modules-load.d/v4l2loopback.conf
    rm -f /usr/local/bin/surface-camera-bridge.sh
    rm -f /usr/local/bin/surface-camera-isys-grant.sh
    rm -f /etc/sudoers.d/surface-camera-isys-grant
    rm -f /etc/systemd/user/surface-camera-bridge@.service
    rm -f /etc/udev/rules.d/91-v4l2loopback-surface.rules
    # Remove WirePlumber ISYS disable rule
    local wp_conf_dir
    if [ -n "$real_user" ]; then
        wp_conf_dir="$(eval echo "~$real_user")/.config/wireplumber/wireplumber.conf.d"
    else
        wp_conf_dir="$HOME/.config/wireplumber/wireplumber.conf.d"
    fi
    rm -f "$wp_conf_dir/50-disable-ipu7-isys.conf"
    udevadm control --reload-rules 2>/dev/null || true
    if lsmod | grep -q v4l2loopback; then
        modprobe -r v4l2loopback 2>/dev/null || true
    fi
    info "Removed v4l2loopback configuration, bridge scripts, and services"

    # Note: we don't remove ipu7-camera-bins libs/firmware or kernel modules
    # since other components might use them. The user can remove those manually.
    warn "Intel proprietary libraries, firmware, and kernel modules were NOT removed."
    warn "Remove manually if desired:"
    warn "  Libraries: /usr/lib/libia_*.so /usr/lib/libcca.so"
    warn "  Firmware:  /lib/firmware/intel/ipu/"
    warn "  Modules:   find /lib/modules/$(uname -r)/updates -name 'intel-ipu7*' -delete && depmod -a"

    info "Done. Reboot to unload kernel modules."
}

# ─── AIQB helpers ─────────────────────────────────────────────────────────────

extract_aiqb_from_firmware() {
    local fw_path="$1"
    local extract_dir="$BUILD_DIR/fw_extract"
    mkdir -p "$extract_dir"

    info "Extracting firmware package..."

    # Try msiextract (from msitools package) or 7z
    if command -v msiextract &>/dev/null; then
        (cd "$extract_dir" && msiextract "$fw_path" 2>/dev/null) || true
    elif command -v 7z &>/dev/null; then
        7z x -o"$extract_dir" "$fw_path" -y >/dev/null 2>&1 || true
    else
        error "Neither msiextract nor 7z found."
        error "Install one: sudo apt install msitools   (or)   sudo apt install p7zip-full"
        return 1
    fi

    local all_found=true
    for f in "${AIQB_FILES[@]}"; do
        local found
        found=$(find "$extract_dir" -name "$f" -type f 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            cp "$found" "$HAL_CONFIG_DIR/$f"
            info "Installed genuine AIQB: $f ($(du -h "$HAL_CONFIG_DIR/$f" | cut -f1))"
        else
            warn "Could not find $f in firmware package."
            all_found=false
        fi
    done

    if ! $all_found; then
        warn "Some AIQB files were not found in this firmware version."
        warn "The firmware package format may have changed."
        return 1
    fi

    return 0
}

install_fallback_aiqb() {
    # Use the upstream OV08X40 AIQB as a fallback for both sensors.
    # It's GRBG Bayer with a noise model — not sensor-specific but functional.
    if [ ! -f "$HAL_CONFIG_DIR/$FALLBACK_AIQB" ]; then
        error "Fallback AIQB not found: $HAL_CONFIG_DIR/$FALLBACK_AIQB"
        error "Ensure the camera HAL was installed correctly in Step 3."
        return 1
    fi

    for f in "${AIQB_FILES[@]}"; do
        if [ ! -f "$HAL_CONFIG_DIR/$f" ]; then
            cp "$HAL_CONFIG_DIR/$FALLBACK_AIQB" "$HAL_CONFIG_DIR/$f"
            info "Installed fallback: $f (copy of $FALLBACK_AIQB)"
        fi
    done

    warn ""
    warn "Using upstream OV08X40 AIQB as fallback. The cameras will work but:"
    warn "  - Color accuracy will be reduced (wrong color matrices/lens shading)"
    warn "  - Noise reduction will be less effective (wrong noise model)"
    warn "  - Auto white balance may have a color cast"
    warn ""
    warn "For best quality, re-run with the Microsoft firmware .msi:"
    warn "  sudo $0 --firmware=/path/to/SurfacePro11withIntel_*.msi"
}

# ─── Install ──────────────────────────────────────────────────────────────────

do_install() {
    # --- Prerequisite checks ---
    if [[ $EUID -ne 0 ]]; then
        error "This script must be run as root (sudo ./install.sh)"
        exit 1
    fi

    info "Checking prerequisites..."

    local missing_pkgs=()
    for cmd in cmake make gcc g++ autoconf automake libtoolize pkg-config git; do
        if ! command -v "$cmd" &>/dev/null; then
            missing_pkgs+=("$cmd")
        fi
    done

    if ! pkg-config --exists gstreamer-1.0 2>/dev/null; then
        missing_pkgs+=("libgstreamer1.0-dev")
    fi
    if ! pkg-config --exists gstreamer-plugins-base-1.0 2>/dev/null; then
        missing_pkgs+=("libgstreamer-plugins-base1.0-dev")
    fi
    if ! pkg-config --exists gstreamer-va-1.0 2>/dev/null || \
       ! pkg-config --exists libva 2>/dev/null || \
       ! pkg-config --exists libva-drm 2>/dev/null; then
        missing_pkgs+=("libva-dev" "libgstreamer-plugins-bad1.0-dev")
    fi

    if [ ${#missing_pkgs[@]} -gt 0 ]; then
        error "Missing build dependencies: ${missing_pkgs[*]}"
        error "Install them first, e.g.:"
        error "  sudo apt install build-essential cmake autoconf automake libtool pkg-config \\"
        error "    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \\"
        error "    libgstreamer-plugins-bad1.0-dev libva-dev git"
        exit 1
    fi

    local kernel_src="/lib/modules/$(uname -r)/build"
    if [ ! -d "$kernel_src" ]; then
        warn "Kernel headers not found: $kernel_src"
        warn "Install them first, e.g.:"
        warn "  sudo apt install linux-headers-$(uname -r)"
        warn "IPU7 kernel module build (Step 1) will be skipped."
    fi

    mkdir -p "$BUILD_DIR"
    info "Build directory: $BUILD_DIR"

    # --- Step 1: Build and install ipu7-drivers (out-of-tree kernel modules) ---
    info ""
    info "=== Step 1/7: IPU7 kernel modules (ipu7-drivers) ==="

    if lsmod | grep -q intel_ipu7_psys; then
        info "intel_ipu7_psys module already loaded."
        info "To rebuild, unload the module first or reboot after install."
    fi

    if [ ! -d "$kernel_src" ]; then
        warn "Skipping: kernel headers not found at $kernel_src"
    elif confirm_step "Build and install IPU7 out-of-tree kernel modules?"; then
        if [ ! -d "$BUILD_DIR/ipu7-drivers" ]; then
            info "Cloning ipu7-drivers..."
            git clone --depth=1 "$DRIVERS_REPO" "$BUILD_DIR/ipu7-drivers"
        fi

        # Apply out-of-tree kernel patches if available
        local patch_ver
        patch_ver=$(find_driver_patches "$BUILD_DIR/ipu7-drivers")
        if [ -n "$patch_ver" ]; then
            local patch_dir="$BUILD_DIR/ipu7-drivers/patch/$patch_ver/out-of-tree"
            local patch_count
            patch_count=$(ls "$patch_dir"/*.patch 2>/dev/null | wc -l)
            if [ "$patch_count" -gt 0 ]; then
                info "Found $patch_count out-of-tree patches for $patch_ver"
                info "These patches need to be applied to your kernel source tree."
                info "If you built your kernel with linux-surface patches, these are"
                info "likely already applied. Skipping kernel source patching."
                info "(If modules fail to build, apply patches from: $patch_dir)"
            fi
        else
            warn "No matching patch directory found for kernel $(uname -r)"
            warn "Build may fail if your kernel is missing required IPU7 patches."
        fi

        info "Building IPU7 kernel modules..."
        cd "$BUILD_DIR/ipu7-drivers"
        make -j"$(nproc)" KERNELRELEASE="$(uname -r)" || {
            error "Module build failed. Check that kernel headers and IPU7 patches are in place."
            error "Kernel source: $kernel_src"
            error "Continuing with remaining steps..."
            cd "$BUILD_DIR"
        }

        if [ -f "$BUILD_DIR/ipu7-drivers/drivers/media/pci/intel/ipu7/intel-ipu7.ko" ] || \
           [ -f "$BUILD_DIR/ipu7-drivers/drivers/media/pci/intel/ipu7/intel-ipu7-psys.ko" ]; then
            info "Installing kernel modules..."
            make modules_install KERNELRELEASE="$(uname -r)"
            depmod -a "$(uname -r)" || warn "depmod failed (non-fatal)"
            info "IPU7 kernel modules installed. A reboot is needed to load them."
        fi
    else
        info "Skipping IPU7 kernel module build."
    fi

    # --- Step 2: Clone and install ipu7-camera-bins ---
    info ""
    info "=== Step 2/7: Intel IPU7 firmware and proprietary libraries ==="

    if [ -f /usr/lib/libcca.so ] && [ -f /lib/firmware/intel/ipu/ipu7x-fw.bin ]; then
        info "Already installed, skipping. (Delete /usr/lib/libcca.so to force reinstall)"
    elif confirm_step "Install Intel IPU7 firmware and proprietary libraries?"; then
        if [ ! -d "$BUILD_DIR/ipu7-camera-bins" ]; then
            info "Cloning ipu7-camera-bins..."
            git clone --depth=1 "$BINS_REPO" "$BUILD_DIR/ipu7-camera-bins"
        fi

        info "Installing firmware..."
        mkdir -p /lib/firmware/intel/ipu
        cp "$BUILD_DIR/ipu7-camera-bins/lib/firmware/intel/ipu/"*.bin /lib/firmware/intel/ipu/

        info "Installing libraries..."
        cp -P "$BUILD_DIR/ipu7-camera-bins/lib/lib"* /usr/lib/

        info "Installing headers..."
        cp -r "$BUILD_DIR/ipu7-camera-bins/include/"* /usr/include/

        info "Installing pkg-config files..."
        mkdir -p /usr/lib/pkgconfig
        cp -r "$BUILD_DIR/ipu7-camera-bins/lib/pkgconfig/"* /usr/lib/pkgconfig/

        ldconfig
        info "Done."
    else
        info "Skipping firmware and library installation."
    fi

    # --- Step 3: Clone, patch, and build ipu7-camera-hal ---
    info ""
    info "=== Step 3/7: Camera HAL (ipu7-camera-hal) ==="

    if confirm_step "Build and install the camera HAL?"; then
        if [ ! -d "$BUILD_DIR/ipu7-camera-hal" ]; then
            info "Cloning ipu7-camera-hal..."
            git clone --depth=1 "$HAL_REPO" "$BUILD_DIR/ipu7-camera-hal"
        fi

        info "Applying Surface Pro 11 patches..."
        cd "$BUILD_DIR/ipu7-camera-hal"
        # Reset to clean upstream state (removes tracked changes AND untracked files
        # from a previous patch attempt — needed because the patch adds new files)
        git checkout -- . 2>/dev/null || true
        git clean -fd 2>/dev/null || true
        git apply "$SCRIPT_DIR/ipu7-camera-hal.patch" || {
            error "Failed to apply HAL patch. The upstream repo may have changed."
            error "Check $SCRIPT_DIR/ipu7-camera-hal.patch against the current repo."
            exit 1
        }
        info "Patch applied successfully."

        info "Building camera HAL..."
        mkdir -p build && cd build
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_INSTALL_PREFIX="$HAL_INSTALL_PREFIX" \
              -DCMAKE_INSTALL_LIBDIR=lib \
              -DBUILD_CAMHAL_ADAPTOR=ON \
              -DBUILD_CAMHAL_PLUGIN=ON \
              -DIPU_VERSIONS="ipu7x;ipu75xa;ipu8" \
              -DUSE_STATIC_GRAPH=ON \
              -DUSE_STATIC_GRAPH_AUTOGEN=ON ..
        make -j"$(nproc)"
        make install
        info "Camera HAL installed."
    else
        info "Skipping camera HAL build."
    fi

    # --- Step 4: Build GStreamer plugin ---
    info ""
    info "=== Step 4/7: GStreamer plugin (icamerasrc) ==="

    if [ -f /usr/lib/gstreamer-1.0/libgsticamerasrc.so ]; then
        info "Already installed, skipping. (Delete to force reinstall)"
    elif confirm_step "Build and install the GStreamer plugin (icamerasrc)?"; then
        if [ ! -d "$BUILD_DIR/icamerasrc" ]; then
            info "Cloning icamerasrc (branch: $ICAMERASRC_BRANCH)..."
            git clone --depth=1 -b "$ICAMERASRC_BRANCH" "$ICAMERASRC_REPO" "$BUILD_DIR/icamerasrc"
        fi

        cd "$BUILD_DIR/icamerasrc"
        export CHROME_SLIM_CAMHAL=ON
        ./autogen.sh
        ./configure --prefix=/usr --enable-gstdrmformat=yes
        make -j"$(nproc)"
        make install
        info "GStreamer plugin installed."
    else
        info "Skipping GStreamer plugin build."
    fi

    # --- Step 5: v4l2loopback virtual cameras ---
    info ""
    info "=== Step 5/7: v4l2loopback virtual cameras ==="
    info "Creates virtual /dev/video devices so Firefox, Zoom, Chrome, etc."
    info "can use the IPU7 cameras (which are only accessible via GStreamer)."

    if confirm_step "Set up v4l2loopback virtual cameras?"; then
        # Check if v4l2loopback is available
        if ! modinfo v4l2loopback &>/dev/null; then
            warn "v4l2loopback kernel module not found."
            echo ""
            echo "  Install it with:"
            echo "    sudo apt install v4l2loopback-dkms    # Debian/Ubuntu"
            echo "    sudo dnf install v4l2loopback          # Fedora"
            echo ""
            echo -n "  Try to install v4l2loopback-dkms now? [Y/n] "
            local v4l2_reply
            read -r v4l2_reply
            if [[ -z "$v4l2_reply" || "$v4l2_reply" =~ ^[Yy]$ ]]; then
                if command -v apt-get &>/dev/null; then
                    apt-get install -y v4l2loopback-dkms || {
                        error "Failed to install v4l2loopback-dkms."
                        warn "Skipping v4l2loopback setup."
                    }
                elif command -v dnf &>/dev/null; then
                    dnf install -y v4l2loopback || {
                        error "Failed to install v4l2loopback."
                        warn "Skipping v4l2loopback setup."
                    }
                else
                    error "No supported package manager found. Install v4l2loopback manually."
                    warn "Skipping v4l2loopback setup."
                fi
            else
                warn "Skipping v4l2loopback setup."
            fi
        fi

        if modinfo v4l2loopback &>/dev/null; then
            # Install config files from the contrib directory
            info "Configuring v4l2loopback module..."
            cp "$SCRIPT_DIR/v4l2loopback-surface.conf" /etc/modprobe.d/v4l2loopback-surface.conf
            info "Installed /etc/modprobe.d/v4l2loopback-surface.conf"

            cp "$SCRIPT_DIR/v4l2loopback.conf" /etc/modules-load.d/v4l2loopback.conf
            info "Installed /etc/modules-load.d/v4l2loopback.conf"

            install -m 755 "$SCRIPT_DIR/surface-camera-bridge.sh" /usr/local/bin/surface-camera-bridge.sh
            info "Installed /usr/local/bin/surface-camera-bridge.sh"

            install -m 755 "$SCRIPT_DIR/surface-camera-isys-grant.sh" /usr/local/bin/surface-camera-isys-grant.sh
            info "Installed /usr/local/bin/surface-camera-isys-grant.sh"

            install -m 440 "$SCRIPT_DIR/surface-camera-sudoers" /etc/sudoers.d/surface-camera-isys-grant
            info "Installed /etc/sudoers.d/surface-camera-isys-grant (passwordless sudo for ISYS grant)"

            mkdir -p /etc/systemd/user
            cp "$SCRIPT_DIR/surface-camera-bridge@.service" /etc/systemd/user/surface-camera-bridge@.service
            info "Installed systemd user service template"

            cp "$SCRIPT_DIR/91-v4l2loopback-surface.rules" /etc/udev/rules.d/91-v4l2loopback-surface.rules
            udevadm control --reload-rules 2>/dev/null || true
            info "Installed /etc/udev/rules.d/91-v4l2loopback-surface.rules"

            # Load the module now (unload first if already loaded with different params)
            if lsmod | grep -q v4l2loopback; then
                info "v4l2loopback already loaded. Reloading with new config..."
                modprobe -r v4l2loopback 2>/dev/null || true
            fi
            modprobe v4l2loopback || warn "Could not load v4l2loopback (will work after reboot)"

            if [ -e /dev/video50 ] && [ -e /dev/video51 ]; then
                info "Virtual cameras ready: /dev/video50 (front), /dev/video51 (rear)"
            fi

            # Hide the 32 raw IPU7 ISYS capture nodes from PipeWire/WirePlumber.
            # These are non-functional for normal apps and confuse camera selection.
            local real_user="${SUDO_USER:-$USER}"
            local wp_conf_dir
            if [ -n "$real_user" ]; then
                wp_conf_dir="$(eval echo "~$real_user")/.config/wireplumber/wireplumber.conf.d"
            else
                wp_conf_dir="$HOME/.config/wireplumber/wireplumber.conf.d"
            fi
            mkdir -p "$wp_conf_dir"
            cp "$SCRIPT_DIR/50-disable-ipu7-isys.conf" "$wp_conf_dir/50-disable-ipu7-isys.conf"
            if [ -n "$real_user" ]; then
                chown -R "$real_user:$(id -gn "$real_user")" "$wp_conf_dir"
            fi
            info "Installed WirePlumber rule to hide raw IPU7 ISYS nodes"

            echo ""
            info "The camera bridge must be running for apps to use the virtual cameras."
            info "v4l2loopback requires an active writer — on-demand activation is not possible."
            echo ""
            echo "  /dev/video50 — Surface Front Camera (imx681-uf)"
            echo "  /dev/video51 — Surface Rear Camera (ov13858-uf)"
            echo ""
            echo "  Start manually:  systemctl --user start surface-camera-bridge@imx681-uf"
            echo "  Stop manually:   systemctl --user stop surface-camera-bridge@imx681-uf"
            echo ""
            info "You can also enable auto-start on login (camera will always be active):"
            if confirm_step "Auto-start the front camera bridge on login?"; then
                local real_user="${SUDO_USER:-$USER}"
                local real_uid
                real_uid=$(id -u "$real_user" 2>/dev/null || echo "")
                if [ -n "$real_uid" ]; then
                    sudo -u "$real_user" XDG_RUNTIME_DIR="/run/user/$real_uid" \
                        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$real_uid/bus" \
                        systemctl --user enable surface-camera-bridge@imx681-uf 2>/dev/null || {
                        warn "Could not enable service. Run as your user:"
                        warn "  systemctl --user enable surface-camera-bridge@imx681-uf"
                    }
                else
                    warn "Could not determine user. Run manually:"
                    warn "  systemctl --user enable surface-camera-bridge@imx681-uf"
                fi
                info "Enabled front camera auto-start for user ${real_user:-$USER}"
            fi
            if confirm_step "Auto-start the rear camera bridge on login?"; then
                local real_user="${SUDO_USER:-$USER}"
                local real_uid
                real_uid=$(id -u "$real_user" 2>/dev/null || echo "")
                if [ -n "$real_uid" ]; then
                    sudo -u "$real_user" XDG_RUNTIME_DIR="/run/user/$real_uid" \
                        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$real_uid/bus" \
                        systemctl --user enable surface-camera-bridge@ov13858-uf 2>/dev/null || {
                        warn "Could not enable service. Run as your user:"
                        warn "  systemctl --user enable surface-camera-bridge@ov13858-uf"
                    }
                else
                    warn "Could not determine user. Run manually:"
                    warn "  systemctl --user enable surface-camera-bridge@ov13858-uf"
                fi
                info "Enabled rear camera auto-start for user ${real_user:-$USER}"
            fi
        fi
    else
        info "Skipping v4l2loopback setup."
    fi

    # --- Step 6: AIQD persistence ---
    info ""
    info "=== Step 6/7: AIQD calibration persistence ==="

    if [ ! -f /etc/tmpfiles.d/camera-hal.conf ]; then
        echo 'd /run/camera 0755 root root -' > /etc/tmpfiles.d/camera-hal.conf
        systemd-tmpfiles --create 2>/dev/null || true
        info "AIQD persistence configured (/run/camera/)."
    else
        info "Already configured."
    fi

    # --- Step 7: AIQB tuning files (last step — needs HAL installed first) ---
    info ""
    info "=== Step 7/7: AIQB tuning files ==="

    local aiqb_missing=false
    for f in "${AIQB_FILES[@]}"; do
        if [ ! -f "$HAL_CONFIG_DIR/$f" ]; then
            aiqb_missing=true
            break
        fi
    done

    if ! $aiqb_missing; then
        info "AIQB files already installed:"
        for f in "${AIQB_FILES[@]}"; do
            info "  $f ($(du -h "$HAL_CONFIG_DIR/$f" | cut -f1))"
        done
    elif [ "${SKIP_AIQB:-}" = "true" ]; then
        info "Skipping AIQB extraction (--skip-aiqb). Installing upstream fallback."
        install_fallback_aiqb
    elif [ -n "${MS_FW_LOCAL:-}" ]; then
        # --firmware=PATH was given on the command line
        if [ -f "${MS_FW_LOCAL}" ]; then
            if ! extract_aiqb_from_firmware "${MS_FW_LOCAL}"; then
                warn "Extraction failed. Installing upstream fallback."
                install_fallback_aiqb
            fi
        else
            error "Firmware file not found: ${MS_FW_LOCAL}"
            warn "Installing upstream fallback instead."
            install_fallback_aiqb
        fi
    else
        # Interactive: ask the user for a path
        info "AIQB tuning files provide sensor-specific ISP calibration data"
        info "(color matrices, lens shading, noise model, auto-exposure curves)."
        info ""
        info "For best image quality, these should be extracted from the Microsoft"
        info "Surface Pro 11 (Intel) firmware update package (.msi file)."
        echo ""
        echo "  To get the firmware .msi file:"
        echo "    1. Go to: $MS_FW_PAGE"
        echo "    2. Click 'Download' and save the .msi file"
        echo "       (filename looks like: SurfacePro11withIntel_Win11_*.msi)"
        echo "    3. Enter the path below, or press Enter to use the fallback"
        echo ""
        echo "  Alternatively, re-run later with:"
        echo "    sudo $0 --firmware=/path/to/SurfacePro11withIntel_*.msi"
        echo ""
        echo -n "  Path to firmware .msi (or Enter for fallback): "
        local fw_path
        read -r fw_path

        if [ -n "$fw_path" ] && [ -f "$fw_path" ]; then
            if ! extract_aiqb_from_firmware "$fw_path"; then
                warn "Extraction failed. Installing upstream fallback."
                install_fallback_aiqb
            fi
        elif [ -n "$fw_path" ]; then
            error "File not found: $fw_path"
            warn "Installing upstream fallback instead."
            install_fallback_aiqb
        else
            info "No firmware provided. Installing upstream fallback."
            install_fallback_aiqb
        fi
    fi

    # --- Done ---
    echo ""
    info "============================================"
    info "Installation complete!"
    info "============================================"
    echo ""
    echo "Next steps:"
    echo "  1. Reboot to load firmware and kernel modules"
    echo ""
    echo "  2. Test the front camera:"
    echo "     rm -f /dev/shm/sem.camlock"
    echo "     GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0 \\"
    echo "       gst-launch-1.0 icamerasrc device-name=imx681-uf num-buffers=30 \\"
    echo "       ! 'video/x-raw,format=NV12,width=1920,height=1080' \\"
    echo "       ! videoconvert ! autovideosink"
    echo ""
    echo "  3. Test the rear camera:"
    echo "     GST_PLUGIN_PATH=/usr/lib/gstreamer-1.0 \\"
    echo "       gst-launch-1.0 icamerasrc device-name=ov13858-uf num-buffers=30 \\"
    echo "       ! 'video/x-raw,format=NV12,width=1920,height=1080' \\"
    echo "       ! videoconvert ! autovideosink"
    echo ""
    echo "  4. Check installation status:"
    echo "     sudo $0 --status"
    echo ""
    echo "  5. Clean up build directory (optional, saves ~3 GB):"
    echo "     rm -rf $BUILD_DIR"
    echo ""
    echo "To uninstall:"
    echo "  sudo $0 --remove"
}

# ─── Dry run ─────────────────────────────────────────────────────────────────

do_dry_run() {
    echo ""
    echo "=== DRY RUN: Surface Pro 11 Camera Install ==="
    echo "No changes will be made. Showing what would happen."
    echo ""

    # --- Prerequisites ---
    dry "Checking prerequisites..."
    local missing_pkgs=()
    for cmd in cmake make gcc g++ autoconf automake libtoolize pkg-config git; do
        if ! command -v "$cmd" &>/dev/null; then
            missing_pkgs+=("$cmd")
        fi
    done
    if ! pkg-config --exists gstreamer-1.0 2>/dev/null; then
        missing_pkgs+=("libgstreamer1.0-dev")
    fi
    if ! pkg-config --exists gstreamer-plugins-base-1.0 2>/dev/null; then
        missing_pkgs+=("libgstreamer-plugins-base1.0-dev")
    fi
    if ! pkg-config --exists gstreamer-va-1.0 2>/dev/null || \
       ! pkg-config --exists libva 2>/dev/null || \
       ! pkg-config --exists libva-drm 2>/dev/null; then
        missing_pkgs+=("libva-dev" "libgstreamer-plugins-bad1.0-dev")
    fi
    if [ ${#missing_pkgs[@]} -gt 0 ]; then
        warn "Missing build dependencies: ${missing_pkgs[*]}"
        warn "Install would FAIL at this point."
    else
        dry "  All build dependencies present."
    fi

    if lsmod | grep -q intel_ipu7; then
        dry "  intel_ipu7 kernel module loaded."
    else
        warn "  intel_ipu7 kernel module NOT loaded."
    fi

    # --- Step 1: ipu7-drivers ---
    echo ""
    dry "=== Step 1/7: IPU7 kernel modules (ipu7-drivers) ==="
    local kernel_src="/lib/modules/$(uname -r)/build"
    if [ ! -d "$kernel_src" ]; then
        warn "  Kernel headers not found: $kernel_src"
        warn "  Module build would be SKIPPED."
    else
        dry "  Kernel headers: $kernel_src"
        if lsmod | grep -q intel_ipu7_psys; then
            dry "  intel_ipu7_psys already loaded."
        fi
        dry "  Would clone: $DRIVERS_REPO"
        local patch_ver
        patch_ver=$(find_driver_patches "$BUILD_DIR/ipu7-drivers" 2>/dev/null || true)
        if [ -n "$patch_ver" ]; then
            dry "  Would use patches from: $patch_ver/out-of-tree/"
        else
            dry "  No local ipu7-drivers clone yet; patch detection deferred to build time."
        fi
        dry "  Would build with: make -j$(nproc)"
        dry "  Would install to: /lib/modules/$(uname -r)/updates/"
        dry "  Would run: depmod -a"
    fi

    # --- Step 2: ipu7-camera-bins ---
    echo ""
    dry "=== Step 2/7: Intel IPU7 firmware and proprietary libraries ==="
    if [ -f /usr/lib/libcca.so ] && [ -f /lib/firmware/intel/ipu/ipu7x-fw.bin ]; then
        dry "  Already installed (would skip)."
    else
        dry "  Would clone: $BINS_REPO"
        dry "  Would install firmware to /lib/firmware/intel/ipu/"
        dry "  Would install libraries to /usr/lib/"
        dry "  Would install headers to /usr/include/"
        dry "  Would install pkg-config to /usr/lib/pkgconfig/"
        dry "  Would run ldconfig"
    fi

    # --- Step 3: ipu7-camera-hal ---
    echo ""
    dry "=== Step 3/7: Camera HAL (ipu7-camera-hal) ==="
    dry "  Would clone: $HAL_REPO"
    dry "  Would apply patch: $SCRIPT_DIR/ipu7-camera-hal.patch"
    if [ -f "$SCRIPT_DIR/ipu7-camera-hal.patch" ]; then
        dry "  Patch file exists ($(du -h "$SCRIPT_DIR/ipu7-camera-hal.patch" | cut -f1))"
    else
        warn "  Patch file MISSING: $SCRIPT_DIR/ipu7-camera-hal.patch"
        warn "  Install would FAIL at this point."
    fi
    dry "  Would build with CMake (prefix=$HAL_INSTALL_PREFIX)"
    dry "  Would install to: $HAL_CONFIG_DIR/"
    dry "  Files from patch:"
    dry "    sensors/imx681-uf.json"
    dry "    sensors/ov13858-uf.json"
    dry "    gcss/IMX681_MSHW0580.IPU7X.bin"
    dry "    gcss/OV13858_MSHW0580.IPU7X.bin"
    dry "    libcamhal_configs.json"

    # --- Step 4: icamerasrc ---
    echo ""
    dry "=== Step 4/7: GStreamer plugin (icamerasrc) ==="
    if [ -f /usr/lib/gstreamer-1.0/libgsticamerasrc.so ]; then
        dry "  Already installed (would skip)."
    else
        dry "  Would clone: $ICAMERASRC_REPO (branch: $ICAMERASRC_BRANCH)"
        dry "  Would build with autotools (CHROME_SLIM_CAMHAL=ON)"
        dry "  Would install to: /usr/lib/gstreamer-1.0/libgsticamerasrc.so"
    fi

    # --- Step 5: v4l2loopback ---
    echo ""
    dry "=== Step 5/7: v4l2loopback virtual cameras ==="
    if modinfo v4l2loopback &>/dev/null; then
        dry "  v4l2loopback module available."
    else
        warn "  v4l2loopback module NOT found. Would offer to install."
    fi
    if [ -f /etc/modprobe.d/v4l2loopback-surface.conf ]; then
        dry "  Already configured."
    else
        dry "  Would create /etc/modprobe.d/v4l2loopback-surface.conf (2 devices: video50, video51)"
        dry "  Would create /etc/modules-load.d/v4l2loopback.conf"
        dry "  Would install /usr/local/bin/surface-camera-bridge.sh"
        dry "  Would install /usr/local/bin/surface-camera-isys-grant.sh"
        dry "  Would install /etc/sudoers.d/surface-camera-isys-grant"
        dry "  Would install /etc/systemd/user/surface-camera-bridge@.service"
        dry "  Would install /etc/udev/rules.d/91-v4l2loopback-surface.rules"
        dry "  Would load v4l2loopback module"
        dry "  Would offer to auto-start camera bridges on login"
    fi

    # --- Step 6: AIQD persistence ---
    echo ""
    dry "=== Step 6/7: AIQD calibration persistence ==="
    if [ -f /etc/tmpfiles.d/camera-hal.conf ]; then
        dry "  Already configured."
    else
        dry "  Would create /etc/tmpfiles.d/camera-hal.conf"
        dry "  Would run systemd-tmpfiles --create"
    fi

    # --- Step 7: AIQB ---
    echo ""
    dry "=== Step 7/7: AIQB tuning files ==="
    local aiqb_missing=false
    for f in "${AIQB_FILES[@]}"; do
        if [ -f "$HAL_CONFIG_DIR/$f" ]; then
            dry "  Already installed: $f ($(du -h "$HAL_CONFIG_DIR/$f" | cut -f1))"
        else
            dry "  Missing: $f"
            aiqb_missing=true
        fi
    done
    if $aiqb_missing; then
        if [ "${SKIP_AIQB:-}" = "true" ]; then
            dry "  Would use upstream fallback AIQB ($FALLBACK_AIQB)"
        elif [ -n "${MS_FW_LOCAL:-}" ]; then
            if [ -f "${MS_FW_LOCAL:-}" ]; then
                dry "  Would extract from local firmware: $MS_FW_LOCAL"
            else
                warn "  Firmware file not found: $MS_FW_LOCAL"
            fi
        else
            dry "  Would prompt for path to Microsoft firmware .msi"
            dry "  Download page: $MS_FW_PAGE"
        fi
        if ! command -v msiextract &>/dev/null && ! command -v 7z &>/dev/null; then
            warn "  Neither msiextract nor 7z found — extraction would FAIL."
            warn "  Install: sudo apt install msitools"
        else
            dry "  Extraction tool available: $(command -v msiextract || command -v 7z)"
        fi
    fi

    # --- Currently installed state ---
    echo ""
    dry "=== Current installation state ==="
    echo ""
    echo "Firmware:"
    for f in /lib/firmware/intel/ipu/ipu7x-fw.bin; do
        [ -f "$f" ] && echo "  [OK] $f" || echo "  [--] $f"
    done
    echo "Libraries:"
    for lib in libcamhal.so libcca.so libia_aic.so libia_lard.so; do
        [ -f "/usr/lib/$lib" ] && echo "  [OK] /usr/lib/$lib" || echo "  [--] /usr/lib/$lib"
    done
    echo "HAL config:"
    for f in libcamhal_configs.json sensors/imx681-uf.json sensors/ov13858-uf.json \
             gcss/IMX681_MSHW0580.IPU7X.bin gcss/OV13858_MSHW0580.IPU7X.bin; do
        [ -f "$HAL_CONFIG_DIR/$f" ] && echo "  [OK] $HAL_CONFIG_DIR/$f" || echo "  [--] $HAL_CONFIG_DIR/$f"
    done
    echo "AIQB:"
    for f in "${AIQB_FILES[@]}"; do
        [ -f "$HAL_CONFIG_DIR/$f" ] && echo "  [OK] $HAL_CONFIG_DIR/$f ($(du -h "$HAL_CONFIG_DIR/$f" | cut -f1))" || echo "  [--] $HAL_CONFIG_DIR/$f"
    done
    echo "GStreamer:"
    [ -f /usr/lib/gstreamer-1.0/libgsticamerasrc.so ] && echo "  [OK] libgsticamerasrc.so" || echo "  [--] libgsticamerasrc.so"
    echo "AIQD:"
    [ -f /etc/tmpfiles.d/camera-hal.conf ] && echo "  [OK] /etc/tmpfiles.d/camera-hal.conf" || echo "  [--] /etc/tmpfiles.d/camera-hal.conf"
    echo "v4l2loopback:"
    [ -f /etc/modprobe.d/v4l2loopback-surface.conf ] && echo "  [OK] modprobe config" || echo "  [--] modprobe config"
    [ -f /usr/local/bin/surface-camera-bridge.sh ] && echo "  [OK] bridge script" || echo "  [--] bridge script"
    [ -f /usr/local/bin/surface-camera-isys-grant.sh ] && echo "  [OK] ISYS grant helper" || echo "  [--] ISYS grant helper"
    [ -f /etc/sudoers.d/surface-camera-isys-grant ] && echo "  [OK] sudoers rule" || echo "  [--] sudoers rule"
    [ -f /etc/systemd/user/surface-camera-bridge@.service ] && echo "  [OK] systemd service" || echo "  [--] systemd service"
    lsmod | grep -q v4l2loopback && echo "  [OK] module loaded" || echo "  [--] module not loaded"
    echo ""
}

# ─── Main ─────────────────────────────────────────────────────────────────────

case "$ACTION" in
    install)
        if $DRY_RUN; then
            do_dry_run
        else
            do_install
        fi
        ;;
    remove)  do_remove ;;
    status)  check_status ;;
esac
