#!/bin/bash
# Surface Pro 11 (Intel) Suspend Fix Installer
#
# Installs:
#   1. Display-blanking service (prevents hang on suspend)
#   2. Resume inhibitor service (prevents lid bounce re-suspend)
#   3. Logind lid switch debounce config
#   4. Intel WiFi power saving config
#
# Usage:
#   sudo ./install.sh          # Install
#   sudo ./install.sh --remove # Uninstall
#   sudo ./install.sh --dry-run # Show what would be done without making changes

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Destination paths
PRE_SUSPEND_SCRIPT="/usr/local/bin/surface-pre-suspend.sh"
DISPLAY_OFF_SERVICE="/etc/systemd/system/surface-display-off.service"
RESUME_INHIBIT_SERVICE="/etc/systemd/system/surface-resume-inhibit.service"
LOGIND_CONF_DIR="/etc/systemd/logind.conf.d"
LOGIND_CONF="$LOGIND_CONF_DIR/logind-surface-lid-debounce.conf"
IWLWIFI_CONF="/etc/modprobe.d/iwlwifi-powersave.conf"

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
            echo "  --remove   Uninstall suspend fix components"
            echo "  --dry-run  Show what would be done without making changes"
            exit 0
            ;;
        *) error "Unknown option: $arg"; exit 1 ;;
    esac
done

# --- Dry run ---
if $DRY_RUN; then
    echo ""
    echo "=== DRY RUN: Surface Pro 11 Suspend Fix Install ==="
    echo "No changes will be made. Showing what would happen."
    echo ""

    # Check source files
    dry "Checking source files..."
    for f in surface-pre-suspend.sh surface-display-off.service \
             surface-resume-inhibit.service logind-surface-lid-debounce.conf \
             iwlwifi-powersave.conf; do
        if [[ -f "$SCRIPT_DIR/$f" ]]; then
            dry "  [OK] $SCRIPT_DIR/$f"
        else
            warn "  MISSING: $SCRIPT_DIR/$f — install would FAIL."
        fi
    done

    echo ""
    dry "=== Would install ==="
    dry "  $SCRIPT_DIR/surface-pre-suspend.sh -> $PRE_SUSPEND_SCRIPT (chmod +x)"
    dry "  $SCRIPT_DIR/surface-display-off.service -> $DISPLAY_OFF_SERVICE"
    dry "  $SCRIPT_DIR/surface-resume-inhibit.service -> $RESUME_INHIBIT_SERVICE"
    dry "  $SCRIPT_DIR/logind-surface-lid-debounce.conf -> $LOGIND_CONF"
    dry "  $SCRIPT_DIR/iwlwifi-powersave.conf -> $IWLWIFI_CONF"

    echo ""
    dry "=== Would run ==="
    dry "  systemctl daemon-reload"
    dry "  systemctl enable surface-display-off.service surface-resume-inhibit.service"
    dry "  (logind config takes effect on next reboot)"

    echo ""
    dry "=== Current installation state ==="
    for f in "$PRE_SUSPEND_SCRIPT" "$DISPLAY_OFF_SERVICE" "$RESUME_INHIBIT_SERVICE" \
             "$LOGIND_CONF" "$IWLWIFI_CONF"; do
        [[ -f "$f" ]] && echo "  [OK] $f" || echo "  [--] $f"
    done
    for svc in surface-display-off.service surface-resume-inhibit.service; do
        if systemctl is-enabled "$svc" &>/dev/null; then
            echo "  [OK] $svc enabled"
        else
            echo "  [--] $svc not enabled"
        fi
    done

    echo ""
    dry "=== Required kernel parameters ==="
    dry "  Check /etc/default/grub for:"
    dry "    acpi_sleep=nonvs acpi_osi=\"Windows 2020\" button.lid_init_state=open"
    dry "    pcie_aspm=force pcie_aspm.policy=powersupersave"
    CURRENT_CMDLINE=$(cat /proc/cmdline 2>/dev/null || echo "")
    for param in acpi_sleep=nonvs "acpi_osi=" button.lid_init_state=open pcie_aspm=force; do
        if echo "$CURRENT_CMDLINE" | grep -q "$param"; then
            echo "  [OK] $param present in current cmdline"
        else
            warn "  $param NOT in current kernel cmdline"
        fi
    done
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
    info "Removing Surface Pro 11 suspend fix..."
    systemctl disable surface-display-off.service surface-resume-inhibit.service 2>/dev/null || true
    rm -f "$PRE_SUSPEND_SCRIPT"
    rm -f "$DISPLAY_OFF_SERVICE"
    rm -f "$RESUME_INHIBIT_SERVICE"
    rm -f "$LOGIND_CONF"
    rm -f "$IWLWIFI_CONF"
    systemctl daemon-reload
    info "Removed. Restart logind for lid switch change to take effect:"
    echo "  sudo systemctl restart systemd-logind"
    exit 0
fi

# --- Install ---
info "Installing Surface Pro 11 suspend fix..."

cp "$SCRIPT_DIR/surface-pre-suspend.sh" "$PRE_SUSPEND_SCRIPT"
chmod +x "$PRE_SUSPEND_SCRIPT"
info "Installed: $PRE_SUSPEND_SCRIPT"

cp "$SCRIPT_DIR/surface-display-off.service" "$DISPLAY_OFF_SERVICE"
info "Installed: $DISPLAY_OFF_SERVICE"

cp "$SCRIPT_DIR/surface-resume-inhibit.service" "$RESUME_INHIBIT_SERVICE"
info "Installed: $RESUME_INHIBIT_SERVICE"

mkdir -p "$LOGIND_CONF_DIR"
cp "$SCRIPT_DIR/logind-surface-lid-debounce.conf" "$LOGIND_CONF"
info "Installed: $LOGIND_CONF"

cp "$SCRIPT_DIR/iwlwifi-powersave.conf" "$IWLWIFI_CONF"
info "Installed: $IWLWIFI_CONF"

systemctl daemon-reload
systemctl enable surface-display-off.service surface-resume-inhibit.service
info "Services enabled."

echo ""
info "Done! Suspend fix is active."
echo ""
echo "Make sure these kernel parameters are set in /etc/default/grub:"
echo "  acpi_sleep=nonvs acpi_osi=\"Windows 2020\" button.lid_init_state=open"
echo "  pcie_aspm=force pcie_aspm.policy=powersupersave"
echo ""
warn "The logind config change requires a reboot (or 'sudo systemctl restart systemd-logind'"
warn "from a text console — restarting logind kills all graphical sessions)."
echo ""
echo "To uninstall:"
echo "  sudo $0 --remove"
