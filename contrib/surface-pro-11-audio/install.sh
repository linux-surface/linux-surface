#!/bin/bash
# Surface Pro 11 (Intel) Audio Enhancement Installer
#
# Installs:
#   1. ALSA UCM config for RT1320 DMIC (microphone, not yet upstream)
#   2. PipeWire speaker EQ (8-band frequency correction for internal speakers)
#   3. PipeWire mic noise suppression (RNNoise, optional)
#
# Usage:
#   sudo ./install.sh          # Install
#   sudo ./install.sh --remove # Uninstall
#   sudo ./install.sh --dry-run # Show what would be done without making changes

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF_DIR="/etc/pipewire/filter-chain.conf.d"
SPEAKER_CONF="surface-pro-11-speaker-eq.conf"
MIC_CONF="surface-pro-11-mic-denoise.conf"
UCM_DIR="/usr/share/alsa/ucm2/sof-soundwire"
UCM_DMIC_CONF="rt1320-dmic.conf"
SOUNDWIRE_CARD=""

info()  { echo -e "\033[0;32m[INFO]\033[0m $*"; }
warn()  { echo -e "\033[1;33m[WARN]\033[0m $*"; }
error() { echo -e "\033[0;31m[ERROR]\033[0m $*"; }
dry()   { echo -e "\033[0;36m[DRY-RUN]\033[0m $*"; }

# --- Dry run ---
if [[ "${1:-}" == "--dry-run" ]]; then
    echo ""
    echo "=== DRY RUN: Surface Pro 11 Audio Install ==="
    echo "No changes will be made. Showing what would happen."
    echo ""

    # Prerequisites
    dry "Checking prerequisites..."
    if command -v pipewire &>/dev/null; then
        PW_VERSION=$(pipewire --version 2>/dev/null | grep -oP 'Linked with libpipewire \K[0-9.]+' || echo "unknown")
        dry "  PipeWire version: $PW_VERSION"
    else
        warn "  PipeWire NOT installed — install would FAIL."
    fi

    FC_MODULE=$(find /usr -name "libpipewire-module-filter-chain.so" 2>/dev/null | head -1)
    if [[ -n "$FC_MODULE" ]]; then
        dry "  Filter-chain module: $FC_MODULE"
    else
        warn "  Filter-chain module NOT found — install would FAIL."
    fi

    # RNNoise
    dry "Checking RNNoise LADSPA plugin..."
    RNNOISE_FOUND=false
    for p in /usr/lib/ladspa/librnnoise_ladspa.so \
             /usr/lib/x86_64-linux-gnu/ladspa/librnnoise_ladspa.so \
             /usr/lib64/ladspa/librnnoise_ladspa.so \
             /usr/local/lib/ladspa/librnnoise_ladspa.so; do
        if [[ -f "$p" ]]; then
            RNNOISE_FOUND=true
            dry "  Found: $p"
            break
        fi
    done
    if ! $RNNOISE_FOUND; then
        warn "  RNNoise NOT found — mic denoise would be inactive."
        dry "  Would prompt to install via package manager or build from source."
    fi

    # UCM config
    echo ""
    dry "=== ALSA UCM config ==="
    if [[ -d "$UCM_DIR" ]]; then
        if [[ -f "$UCM_DIR/$UCM_DMIC_CONF" ]]; then
            dry "  Already installed: $UCM_DIR/$UCM_DMIC_CONF (would skip)"
        else
            dry "  Would install: $SCRIPT_DIR/$UCM_DMIC_CONF -> $UCM_DIR/$UCM_DMIC_CONF"
            [[ -f "$SCRIPT_DIR/$UCM_DMIC_CONF" ]] || warn "  Source file MISSING: $SCRIPT_DIR/$UCM_DMIC_CONF"
        fi
    else
        warn "  UCM directory not found: $UCM_DIR"
        warn "  UCM install would be skipped."
    fi

    # PipeWire configs
    echo ""
    dry "=== PipeWire filter-chain configs ==="
    dry "  Would create directory: $CONF_DIR"
    if [[ -f "$SCRIPT_DIR/$SPEAKER_CONF" ]]; then
        dry "  Would install: $SCRIPT_DIR/$SPEAKER_CONF -> $CONF_DIR/$SPEAKER_CONF"
        dry "    (with target.object patched to detected speaker sink)"
    else
        warn "  Source file MISSING: $SCRIPT_DIR/$SPEAKER_CONF"
    fi
    if [[ -f "$SCRIPT_DIR/$MIC_CONF" ]]; then
        dry "  Would install: $SCRIPT_DIR/$MIC_CONF -> $CONF_DIR/$MIC_CONF"
        dry "    (with target.object patched to detected mic source)"
    else
        warn "  Source file MISSING: $SCRIPT_DIR/$MIC_CONF"
    fi

    # Current state
    echo ""
    dry "=== Current installation state ==="
    echo "UCM:"
    [[ -f "$UCM_DIR/$UCM_DMIC_CONF" ]] && echo "  [OK] $UCM_DIR/$UCM_DMIC_CONF" || echo "  [--] $UCM_DIR/$UCM_DMIC_CONF"
    echo "PipeWire configs:"
    [[ -f "$CONF_DIR/$SPEAKER_CONF" ]] && echo "  [OK] $CONF_DIR/$SPEAKER_CONF" || echo "  [--] $CONF_DIR/$SPEAKER_CONF"
    [[ -f "$CONF_DIR/$MIC_CONF" ]] && echo "  [OK] $CONF_DIR/$MIC_CONF" || echo "  [--] $CONF_DIR/$MIC_CONF"
    echo ""
    exit 0
fi

# --- Uninstall ---
if [[ "${1:-}" == "--remove" || "${1:-}" == "--uninstall" ]]; then
    info "Removing Surface Pro 11 audio configs..."
    rm -f "$CONF_DIR/$SPEAKER_CONF"
    rm -f "$CONF_DIR/$MIC_CONF"
    if [[ -f "$UCM_DIR/$UCM_DMIC_CONF" ]]; then
        warn "UCM config $UCM_DIR/$UCM_DMIC_CONF left in place (needed for mic to work)."
        warn "Remove manually only if you no longer need the internal microphone."
    fi
    info "Removed PipeWire configs. Restart filter-chain to take effect:"
    echo "  systemctl --user restart filter-chain"
    exit 0
fi

# --- Prerequisite checks ---
info "Checking prerequisites..."

if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root (sudo ./install.sh)"
    exit 1
fi

if ! command -v pipewire &>/dev/null; then
    error "PipeWire not found. Install PipeWire first."
    exit 1
fi

PW_VERSION=$(pipewire --version 2>/dev/null | grep -oP 'Linked with libpipewire \K[0-9.]+' || echo "unknown")
info "PipeWire version: $PW_VERSION"

FC_MODULE=$(find /usr -name "libpipewire-module-filter-chain.so" 2>/dev/null | head -1)
if [[ -z "$FC_MODULE" ]]; then
    error "PipeWire filter-chain module not found."
    error "Install the pipewire filter-chain package for your distribution."
    exit 1
fi
info "Filter-chain module: $FC_MODULE"

# --- RNNoise build-from-source helper ---
RNNOISE_REPO="https://github.com/werman/noise-suppression-for-voice.git"
RNNOISE_BUILD_DIR="/tmp/noise-suppression-for-voice"

build_rnnoise_from_source() {
    # Check build dependencies
    local missing=()
    command -v git   &>/dev/null || missing+=(git)
    command -v cmake &>/dev/null || missing+=(cmake)
    command -v make  &>/dev/null || missing+=(make)
    command -v cc    &>/dev/null || missing+=(gcc)

    if [[ ${#missing[@]} -gt 0 ]]; then
        warn "Missing build dependencies: ${missing[*]}"
        echo "  Install them first:"
        if command -v apt &>/dev/null; then
            echo "    sudo apt install ${missing[*]}"
        elif command -v dnf &>/dev/null; then
            echo "    sudo dnf install ${missing[*]}"
        elif command -v pacman &>/dev/null; then
            echo "    sudo pacman -S ${missing[*]}"
        fi
        return 1
    fi

    info "Cloning noise-suppression-for-voice..."
    rm -rf "$RNNOISE_BUILD_DIR"
    if ! git clone --depth 1 "$RNNOISE_REPO" "$RNNOISE_BUILD_DIR" 2>&1; then
        error "Failed to clone repository."
        return 1
    fi

    info "Building LADSPA plugin (this may take a minute)..."
    mkdir -p "$RNNOISE_BUILD_DIR/build"
    if ! cmake -S "$RNNOISE_BUILD_DIR" -B "$RNNOISE_BUILD_DIR/build" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_LADSPA_PLUGIN=ON \
            -DBUILD_VST_PLUGIN=OFF \
            -DBUILD_VST3_PLUGIN=OFF \
            -DBUILD_LV2_PLUGIN=OFF \
            -DBUILD_AU_PLUGIN=OFF \
            -DBUILD_AUV3_PLUGIN=OFF \
            -DBUILD_TESTS=OFF 2>&1; then
        error "CMake configuration failed."
        rm -rf "$RNNOISE_BUILD_DIR"
        return 1
    fi

    if ! cmake --build "$RNNOISE_BUILD_DIR/build" --target rnnoise_ladspa -j"$(nproc)" 2>&1; then
        error "Build failed."
        rm -rf "$RNNOISE_BUILD_DIR"
        return 1
    fi

    # Find the built library
    local built_lib
    built_lib=$(find "$RNNOISE_BUILD_DIR/build" -name "librnnoise_ladspa.so" -type f | head -1)
    if [[ -z "$built_lib" ]]; then
        error "Build succeeded but librnnoise_ladspa.so not found."
        rm -rf "$RNNOISE_BUILD_DIR"
        return 1
    fi

    # Determine install path
    local ladspa_dir="/usr/lib/ladspa"
    if [[ -d "/usr/lib/x86_64-linux-gnu/ladspa" ]]; then
        ladspa_dir="/usr/lib/x86_64-linux-gnu/ladspa"
    elif [[ -d "/usr/lib64/ladspa" ]]; then
        ladspa_dir="/usr/lib64/ladspa"
    fi
    mkdir -p "$ladspa_dir"

    cp "$built_lib" "$ladspa_dir/librnnoise_ladspa.so"
    info "Installed: $ladspa_dir/librnnoise_ladspa.so"

    RNNOISE_FOUND=true

    # Clean up build directory
    rm -rf "$RNNOISE_BUILD_DIR"
    info "Build directory cleaned up."
}

# Check for RNNoise LADSPA plugin
RNNOISE_FOUND=false
for p in /usr/lib/ladspa/librnnoise_ladspa.so \
         /usr/lib/x86_64-linux-gnu/ladspa/librnnoise_ladspa.so \
         /usr/lib64/ladspa/librnnoise_ladspa.so \
         /usr/local/lib/ladspa/librnnoise_ladspa.so; do
    if [[ -f "$p" ]]; then
        RNNOISE_FOUND=true
        info "RNNoise LADSPA plugin: $p"
        break
    fi
done

if ! $RNNOISE_FOUND; then
    warn "RNNoise LADSPA plugin not found."
    warn "Mic noise suppression requires librnnoise_ladspa.so to work."
    echo ""

    # Try distro package first
    RNNOISE_PKG_INSTALLED=false
    if command -v dnf &>/dev/null; then
        echo "  RNNoise is available as a Fedora package."
        read -rp "  Install via 'dnf install noise-suppression-for-voice'? [y/N] " ans
        if [[ "$ans" =~ ^[Yy] ]]; then
            if dnf install -y noise-suppression-for-voice; then
                RNNOISE_PKG_INSTALLED=true
            fi
        fi
    elif command -v pacman &>/dev/null; then
        echo "  RNNoise may be available in the AUR (noise-suppression-for-voice)."
        echo "  Install it with your AUR helper (e.g. yay -S noise-suppression-for-voice)."
    fi

    # Re-check after package install
    if $RNNOISE_PKG_INSTALLED; then
        for p in /usr/lib/ladspa/librnnoise_ladspa.so \
                 /usr/lib/x86_64-linux-gnu/ladspa/librnnoise_ladspa.so \
                 /usr/lib64/ladspa/librnnoise_ladspa.so \
                 /usr/local/lib/ladspa/librnnoise_ladspa.so; do
            if [[ -f "$p" ]]; then
                RNNOISE_FOUND=true
                info "RNNoise LADSPA plugin: $p"
                break
            fi
        done
    fi

    # Offer to build from source if still not found
    if ! $RNNOISE_FOUND; then
        echo ""
        echo "  RNNoise can be built from source (requires git, cmake, and a C compiler)."
        read -rp "  Clone and build noise-suppression-for-voice from GitHub? [y/N] " ans
        if [[ "$ans" =~ ^[Yy] ]]; then
            build_rnnoise_from_source
        fi
    fi

    if ! $RNNOISE_FOUND; then
        warn ""
        warn "Skipping RNNoise. The speaker EQ will still work."
        warn "Mic noise suppression config will be installed but inactive."
    fi
fi

# --- Detect audio devices ---
info "Detecting audio devices..."

SUDO_USER_ID="${SUDO_UID:-}"
if [[ -n "$SUDO_USER_ID" ]]; then
    REAL_USER=$(id -un "$SUDO_USER_ID" 2>/dev/null || echo "")
else
    REAL_USER=""
fi

detect_device() {
    local pattern="$1"
    local device=""

    if [[ -n "$REAL_USER" ]]; then
        device=$(sudo -u "$REAL_USER" XDG_RUNTIME_DIR="/run/user/$SUDO_USER_ID" \
            pw-cli list-objects 2>/dev/null | grep "node.name" | grep "$pattern" | \
            head -1 | sed 's/.*= "//;s/"//' || true)
    fi

    if [[ -z "$device" ]]; then
        device=$(pw-cli list-objects 2>/dev/null | grep "node.name" | grep "$pattern" | \
            head -1 | sed 's/.*= "//;s/"//' || true)
    fi

    echo "$device"
}

SPEAKER_SINK=$(detect_device "HiFi__Speaker__sink")
MIC_SOURCE=$(detect_device "HiFi__Mic__source")

if [[ -z "$SPEAKER_SINK" ]]; then
    SPEAKER_SINK="alsa_output.pci-0000_00_1f.3-platform-sof_sdw.HiFi__Speaker__sink"
    warn "Could not detect speaker sink, using default: $SPEAKER_SINK"
else
    info "Speaker sink: $SPEAKER_SINK"
fi

if [[ -z "$MIC_SOURCE" ]]; then
    MIC_SOURCE="alsa_input.pci-0000_00_1f.3-platform-sof_sdw.HiFi__Mic__source"
    warn "Could not detect mic source, using default: $MIC_SOURCE"
else
    info "Mic source: $MIC_SOURCE"
fi

# --- Install UCM config ---
info "Installing ALSA UCM config..."

if [[ -d "$UCM_DIR" ]]; then
    if [[ -f "$UCM_DIR/$UCM_DMIC_CONF" ]]; then
        info "UCM config already exists: $UCM_DIR/$UCM_DMIC_CONF (skipping)"
    elif [[ -f "$SCRIPT_DIR/$UCM_DMIC_CONF" ]]; then
        cp "$SCRIPT_DIR/$UCM_DMIC_CONF" "$UCM_DIR/$UCM_DMIC_CONF"
        info "Installed: $UCM_DIR/$UCM_DMIC_CONF"
        info "  (RT1320 SDCA microphone UCM device — not yet upstream in alsa-ucm-conf)"
    else
        error "UCM config not found: $SCRIPT_DIR/$UCM_DMIC_CONF"
        exit 1
    fi
else
    warn "UCM directory not found: $UCM_DIR"
    warn "Skipping UCM install. The internal microphone may not appear until"
    warn "alsa-ucm-conf is installed and this file is placed manually."
fi

# --- Detect SoundWire card number ---
info "Detecting SoundWire card..."

for card_dir in /proc/asound/card*/id; do
    if [[ -f "$card_dir" ]]; then
        card_id=$(cat "$card_dir")
        card_num=$(basename "$(dirname "$card_dir")" | sed 's/card//')
        if [[ "$card_id" == "sofsoundwire" ]]; then
            SOUNDWIRE_CARD="$card_num"
            break
        fi
    fi
done

if [[ -n "$SOUNDWIRE_CARD" ]]; then
    info "SoundWire card: card $SOUNDWIRE_CARD"
else
    warn "SoundWire card not found. Mixer initialization will be skipped."
    warn "If audio doesn't work, ensure the kernel patch is applied and reboot."
fi

# --- Enable mixer switches ---
# The RT1320 codec's output and capture switches default to off after the
# driver loads. UCM profiles should handle this, but on first boot (or if
# UCM isn't fully active yet) the switches may be off, producing no sound.
if [[ -n "$SOUNDWIRE_CARD" ]]; then
    info "Enabling RT1320 mixer switches..."

    enable_switch() {
        local name="$1" val="$2"
        local cur
        cur=$(amixer -c "$SOUNDWIRE_CARD" cget name="$name" 2>/dev/null | grep ': values=' | sed 's/.*: values=//')
        if [[ -n "$cur" ]]; then
            amixer -c "$SOUNDWIRE_CARD" cset name="$name" "$val" >/dev/null 2>&1 && \
                info "  $name = $val" || \
                warn "  Failed to set $name"
        fi
    }

    # Speaker output switches
    enable_switch 'rt1320-1 OT23 L Switch' on
    enable_switch 'rt1320-1 OT23 R Switch' on

    # Microphone capture switch (4 channels: stereo mic × 2 SDCA contexts)
    enable_switch 'rt1320-1 FU Capture Switch' on,on,on,on

    # Persist with alsactl
    if command -v alsactl &>/dev/null; then
        alsactl store "$SOUNDWIRE_CARD" 2>/dev/null && \
            info "  Mixer state saved (alsactl store)" || true
    fi
fi

# --- Install PipeWire configs ---
info "Installing PipeWire filter-chain configs..."

mkdir -p "$CONF_DIR"

# Install speaker EQ (patch target.object with detected sink name)
if [[ -f "$SCRIPT_DIR/$SPEAKER_CONF" ]]; then
    sed "s|target.object.*=.*\".*\"|target.object  = \"$SPEAKER_SINK\"|" \
        "$SCRIPT_DIR/$SPEAKER_CONF" > "$CONF_DIR/$SPEAKER_CONF"
    info "Installed: $CONF_DIR/$SPEAKER_CONF"
else
    error "Speaker config not found: $SCRIPT_DIR/$SPEAKER_CONF"
    exit 1
fi

# Install mic denoise (patch target.object with detected source name)
if [[ -f "$SCRIPT_DIR/$MIC_CONF" ]]; then
    sed "s|target.object.*=.*\".*\"|target.object = \"$MIC_SOURCE\"|" \
        "$SCRIPT_DIR/$MIC_CONF" > "$CONF_DIR/$MIC_CONF"
    info "Installed: $CONF_DIR/$MIC_CONF"
    if ! $RNNOISE_FOUND; then
        warn "  (inactive until librnnoise_ladspa.so is installed)"
    fi
else
    error "Mic config not found: $SCRIPT_DIR/$MIC_CONF"
    exit 1
fi

# --- Activation ---
echo ""
info "Config files installed successfully!"
echo ""

# Helper: run a command as the real user (for user-session systemctl/wpctl)
run_as_user() {
    if [[ -n "$REAL_USER" && -n "$SUDO_USER_ID" ]]; then
        sudo -u "$REAL_USER" XDG_RUNTIME_DIR="/run/user/$SUDO_USER_ID" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$SUDO_USER_ID/bus" \
            "$@"
    else
        "$@"
    fi
}

# Step 1: Restart filter-chain
read -rp "Restart PipeWire filter-chain service now? [Y/n] " ans
if [[ ! "$ans" =~ ^[Nn] ]]; then
    if run_as_user systemctl --user restart filter-chain 2>&1; then
        sleep 2
        if run_as_user systemctl --user is-active --quiet filter-chain 2>/dev/null; then
            info "filter-chain restarted successfully."
        else
            warn "filter-chain restarted but may not be active."
            warn "Check with: systemctl --user status filter-chain"
        fi
    else
        warn "Could not restart filter-chain."
        warn "Run manually: systemctl --user restart filter-chain"
    fi
else
    echo "  Skipped. Run manually later:"
    echo "    systemctl --user restart filter-chain"
fi

# Step 2: Verify virtual devices
echo ""
read -rp "Show audio device status (wpctl status)? [Y/n] " ans
if [[ ! "$ans" =~ ^[Nn] ]]; then
    echo ""
    run_as_user wpctl status 2>&1 | grep -E "Sinks:|Sources:|Filters:|surface_|Surface Pro" || true
    echo ""
fi

# Step 3: Set EQ sink as default
EQ_NODE_ID=$(run_as_user pw-cli list-objects 2>/dev/null | \
    grep -B5 '"surface_speaker_eq_input"' | grep "^[[:space:]]*id " | \
    awk '{print $2}' | tr -d ',' | head -1 || true)

if [[ -n "$EQ_NODE_ID" ]]; then
    echo "The EQ virtual sink is node $EQ_NODE_ID."
    read -rp "Set 'Surface Pro 11 Speaker (EQ)' as the default audio output? [Y/n] " ans
    if [[ ! "$ans" =~ ^[Nn] ]]; then
        if run_as_user wpctl set-default "$EQ_NODE_ID" 2>&1; then
            info "Default sink set to Surface Pro 11 Speaker (EQ)."
            info "All audio will now go through the speaker EQ."
        else
            warn "Could not set default sink."
            warn "Set manually: wpctl set-default $EQ_NODE_ID"
        fi
    else
        echo "  Skipped. Set manually later if desired:"
        echo "    wpctl set-default $EQ_NODE_ID"
    fi
else
    warn "EQ sink node not found. Restart filter-chain first, then set default with:"
    echo "    wpctl set-default \$(pw-cli list-objects | grep -B5 surface_speaker_eq_input | grep 'id ' | awk '{print \$2}' | tr -d ',')"
fi

# Done
echo ""
info "All done!"
echo ""
echo "To uninstall:"
echo "  sudo $0 --remove"
echo "  systemctl --user restart filter-chain"
