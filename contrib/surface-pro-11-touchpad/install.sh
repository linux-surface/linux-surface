#!/bin/bash
# Surface Pro 11 (Intel) Touchpad Fix Installer
#
# Installs:
#   1. udev rule: groups keyboard + touchpad for disable-while-typing (DWT)
#   2. libinput quirk: palm detection tuning for Type Cover touchpad
#
# Usage:
#   sudo ./install.sh          # Install
#   sudo ./install.sh --remove # Uninstall
#   sudo ./install.sh --dry-run # Show what would be done without making changes

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Destination paths
UDEV_RULE="/etc/udev/rules.d/99-surface-touchpad-group.rules"
QUIRKS_DIR="/etc/libinput"
QUIRKS_FILE="$QUIRKS_DIR/local-overrides.quirks"

info()  { echo -e "\033[0;32m[INFO]\033[0m $*"; }
warn()  { echo -e "\033[1;33m[WARN]\033[0m $*"; }
error() { echo -e "\033[0;31m[ERROR]\033[0m $*"; }
dry()   { echo -e "\033[0;36m[DRY-RUN]\033[0m $*"; }

# --- Parse arguments ---
ACTION="install"
DRY_RUN=false
for arg in "$@"; do
    case "$arg" in
        --remove|--uninstall) ACTION="remove" ;;
        --dry-run)            DRY_RUN=true ;;
        --help|-h)
            echo "Usage: sudo $0 [options]"
            echo ""
            echo "Options:"
            echo "  --remove   Uninstall touchpad fix components"
            echo "  --dry-run  Show what would be done without making changes"
            exit 0
            ;;
        *) error "Unknown option: $arg"; exit 1 ;;
    esac
done

# --- Dry run ---
if $DRY_RUN; then
    echo ""
    echo "=== DRY RUN: Surface Pro 11 Touchpad Fix Install ==="
    echo "No changes will be made. Showing what would happen."
    echo ""

    # Check source files
    dry "Checking source files..."
    for f in 99-surface-touchpad-group.rules local-overrides.quirks; do
        if [[ -f "$SCRIPT_DIR/$f" ]]; then
            dry "  [OK] $SCRIPT_DIR/$f"
        else
            warn "  MISSING: $SCRIPT_DIR/$f — install would FAIL."
        fi
    done

    echo ""
    dry "=== Would install ==="
    dry "  $SCRIPT_DIR/99-surface-touchpad-group.rules -> $UDEV_RULE"
    dry "  $SCRIPT_DIR/local-overrides.quirks -> $QUIRKS_FILE"

    echo ""
    dry "=== Would run ==="
    dry "  udevadm control --reload-rules"
    dry "  udevadm trigger --subsystem-match=input"

    echo ""
    dry "=== Current installation state ==="
    [[ -f "$UDEV_RULE" ]] && echo "  [OK] $UDEV_RULE" || echo "  [--] $UDEV_RULE"
    [[ -f "$QUIRKS_FILE" ]] && echo "  [OK] $QUIRKS_FILE" || echo "  [--] $QUIRKS_FILE"

    # Check if touchpad devices are present
    echo ""
    dry "=== Touchpad devices ==="
    if ls /sys/class/input/event*/device/name 2>/dev/null | while read -r f; do
        name=$(cat "$f" 2>/dev/null)
        if echo "$name" | grep -qi "045E:0C8D\|surface.*touchpad"; then
            echo "  [OK] Found touchpad: $name"
            exit 0
        fi
    done; then
        true
    else
        warn "  Surface touchpad device not detected (Type Cover may not be attached)."
    fi
    echo ""
    exit 0
fi

# --- Root check ---
if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root (sudo ./install.sh)"
    exit 1
fi

# --- Uninstall ---
if [[ "$ACTION" == "remove" ]]; then
    info "Removing Surface Pro 11 touchpad fix..."
    rm -f "$UDEV_RULE"
    rm -f "$QUIRKS_FILE"
    udevadm control --reload-rules 2>/dev/null || true
    udevadm trigger --subsystem-match=input 2>/dev/null || true
    info "Removed. Log out and back in for full effect."
    exit 0
fi

# --- Install ---
info "Installing Surface Pro 11 touchpad fix..."

cp "$SCRIPT_DIR/99-surface-touchpad-group.rules" "$UDEV_RULE"
info "Installed: $UDEV_RULE"

mkdir -p "$QUIRKS_DIR"
cp "$SCRIPT_DIR/local-overrides.quirks" "$QUIRKS_FILE"
info "Installed: $QUIRKS_FILE"

udevadm control --reload-rules
udevadm trigger --subsystem-match=input
info "udev rules reloaded."

echo ""
info "Done! Log out and back in (or reboot) for full effect."
echo ""
echo "To uninstall:"
echo "  sudo $0 --remove"
