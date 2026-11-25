#!/bin/bash
#
# surface-hibernate-setup.sh — Configure suspend-then-hibernate for Surface Pro 11
#
# Usage:
#   sudo ./surface-hibernate-setup.sh [--swap=<path>] [--delay=<time>]
#   sudo ./surface-hibernate-setup.sh --dry-run
#   sudo ./surface-hibernate-setup.sh --uninstall
#
# This sets up the kernel resume parameters, systemd sleep config, and logind
# lid switch behavior so the system automatically hibernates after spending
# a configurable time in s2idle (suspend-then-hibernate).
#

set -euo pipefail

GRUB_DROPIN="/etc/default/grub.d/surface-hibernate.cfg"
SLEEP_DROPIN="/etc/systemd/sleep.conf.d/surface-hibernate.conf"
LOGIND_DROPIN="/etc/systemd/logind.conf.d/surface-hibernate.conf"

usage() {
    cat <<'EOF'
Usage:
  sudo ./surface-hibernate-setup.sh [OPTIONS]
  sudo ./surface-hibernate-setup.sh --uninstall

Options:
  --swap=<path>     Path to swap partition or swap file (auto-detected if omitted)
  --delay=<time>    Time in s2idle before hibernating (default: 2h)
                    Uses systemd time syntax: 30min, 1h, 2h, etc.
  --dry-run         Show what would be done without making changes
  --uninstall       Remove all hibernate configuration
  -h, --help        Show this help

Examples:
  sudo ./surface-hibernate-setup.sh                  # auto-detect swap, 2h delay
  sudo ./surface-hibernate-setup.sh --delay=1h       # hibernate after 1 hour
  sudo ./surface-hibernate-setup.sh --swap=/dev/sda2  # use specific swap partition
  sudo ./surface-hibernate-setup.sh --uninstall       # remove configuration
EOF
    exit 0
}

die() { echo "Error: $1" >&2; exit 1; }
info() { echo "==> $1"; }
warn() { echo "Warning: $1" >&2; }

# --- Argument parsing ---
SWAP_PATH=""
DELAY="2h"
UNINSTALL=false
DRY_RUN=false

for arg in "$@"; do
    case "$arg" in
        --swap=*)    SWAP_PATH="${arg#--swap=}" ;;
        --delay=*)   DELAY="${arg#--delay=}" ;;
        --dry-run)   DRY_RUN=true ;;
        --uninstall) UNINSTALL=true ;;
        -h|--help)   usage ;;
        *)           die "Unknown option: $arg" ;;
    esac
done

# --- Root check (skip for dry-run) ---
if ! $DRY_RUN; then
    [[ $EUID -eq 0 ]] || die "This script must be run as root (use sudo)"
fi

# --- Uninstall ---
if $UNINSTALL; then
    info "Removing hibernate configuration..."

    removed=false
    for f in "$GRUB_DROPIN" "$SLEEP_DROPIN" "$LOGIND_DROPIN"; do
        if [[ -f "$f" ]]; then
            rm -v "$f"
            removed=true
        fi
    done

    if $removed; then
        info "Updating GRUB..."
        update-grub

        info "Updating initramfs..."
        update-initramfs -u -k all

        info "Restarting logind..."
        systemctl restart systemd-logind

        echo
        echo "Hibernate configuration removed."
        echo "Reboot for GRUB changes to take effect."
    else
        echo "No hibernate configuration found — nothing to remove."
    fi
    exit 0
fi

# --- Dry run ---
if $DRY_RUN; then
    echo ""
    echo "=== DRY RUN: Hibernate Setup ==="
    echo "No changes will be made. Showing what would happen."
    echo ""

    # Swap detection
    if [[ -z "$SWAP_PATH" ]]; then
        mapfile -t SWAPS < <(swapon --show=NAME --noheadings 2>/dev/null)
        if [[ ${#SWAPS[@]} -eq 0 ]]; then
            warn "No active swap found. Setup would FAIL."
        else
            SWAP_PATH="${SWAPS[0]}"
            info "Auto-detected swap: $SWAP_PATH"
        fi
    fi

    if [[ -n "$SWAP_PATH" && -e "$SWAP_PATH" ]]; then
        if [[ -f "$SWAP_PATH" ]]; then
            info "Swap type: file"
        elif [[ -b "$SWAP_PATH" ]]; then
            info "Swap type: partition"
        fi

        RAM_BYTES=$(free -b | awk '/^Mem:/{print $2}')
        if [[ -f "$SWAP_PATH" ]]; then
            SWAP_BYTES=$(stat -c%s "$SWAP_PATH" 2>/dev/null || echo 0)
        else
            SWAP_BYTES=$(blockdev --getsize64 "$SWAP_PATH" 2>/dev/null || echo 0)
        fi
        RAM_GB=$(awk "BEGIN{printf \"%.1f\", $RAM_BYTES/1073741824}")
        SWAP_GB=$(awk "BEGIN{printf \"%.1f\", $SWAP_BYTES/1073741824}")
        info "RAM: ${RAM_GB} GB, Swap: ${SWAP_GB} GB"
        if [[ "$SWAP_BYTES" -lt "$RAM_BYTES" ]]; then
            warn "Swap is smaller than RAM — hibernate may fail."
        fi
    fi

    if systemd-analyze timespan "$DELAY" &>/dev/null; then
        DELAY_HUMAN=$(systemd-analyze timespan "$DELAY" 2>/dev/null | grep -oP '(?<=Human: ).*' || echo "$DELAY")
        info "Hibernate delay: $DELAY_HUMAN"
    else
        warn "Invalid delay format: $DELAY"
    fi

    echo ""
    info "Would create/overwrite:"
    info "  $GRUB_DROPIN (GRUB kernel params with resume=UUID=...)"
    info "  $SLEEP_DROPIN (HibernateDelaySec=$DELAY)"
    info "  $LOGIND_DROPIN (HandleLidSwitch=suspend-then-hibernate)"
    info "Would run: update-grub"
    info "Would run: update-initramfs -u -k all"
    info "Would run: systemctl restart systemd-logind"

    echo ""
    info "Current state:"
    [[ -f "$GRUB_DROPIN" ]] && echo "  [OK] $GRUB_DROPIN" || echo "  [--] $GRUB_DROPIN"
    [[ -f "$SLEEP_DROPIN" ]] && echo "  [OK] $SLEEP_DROPIN" || echo "  [--] $SLEEP_DROPIN"
    [[ -f "$LOGIND_DROPIN" ]] && echo "  [OK] $LOGIND_DROPIN" || echo "  [--] $LOGIND_DROPIN"
    echo ""
    exit 0
fi

# --- Detect swap ---
if [[ -z "$SWAP_PATH" ]]; then
    mapfile -t SWAPS < <(swapon --show=NAME --noheadings 2>/dev/null)

    if [[ ${#SWAPS[@]} -eq 0 ]]; then
        die "No active swap found. Create a swap partition or swap file first."
    elif [[ ${#SWAPS[@]} -eq 1 ]]; then
        SWAP_PATH="${SWAPS[0]}"
        info "Auto-detected swap: $SWAP_PATH"
    else
        echo "Multiple swap devices found:"
        for i in "${!SWAPS[@]}"; do
            echo "  $((i+1)). ${SWAPS[$i]}"
        done
        read -rp "Select swap device [1-${#SWAPS[@]}]: " choice
        [[ "$choice" =~ ^[0-9]+$ ]] || die "Invalid selection"
        idx=$((choice - 1))
        [[ $idx -ge 0 && $idx -lt ${#SWAPS[@]} ]] || die "Selection out of range"
        SWAP_PATH="${SWAPS[$idx]}"
    fi
fi

[[ -e "$SWAP_PATH" ]] || die "Swap path does not exist: $SWAP_PATH"

# --- Determine if swap file or partition ---
SWAP_TYPE=""
if [[ -f "$SWAP_PATH" ]]; then
    SWAP_TYPE="file"
elif [[ -b "$SWAP_PATH" ]]; then
    SWAP_TYPE="partition"
else
    die "$SWAP_PATH is neither a regular file nor a block device"
fi

info "Swap type: $SWAP_TYPE"

# --- Validate swap size >= RAM ---
RAM_BYTES=$(free -b | awk '/^Mem:/{print $2}')
if [[ "$SWAP_TYPE" == "file" ]]; then
    SWAP_BYTES=$(stat -c%s "$SWAP_PATH" 2>/dev/null || echo 0)
else
    SWAP_BYTES=$(blockdev --getsize64 "$SWAP_PATH" 2>/dev/null || echo 0)
fi

RAM_GB=$(awk "BEGIN{printf \"%.1f\", $RAM_BYTES/1073741824}")
SWAP_GB=$(awk "BEGIN{printf \"%.1f\", $SWAP_BYTES/1073741824}")

if [[ "$SWAP_BYTES" -lt "$RAM_BYTES" ]]; then
    warn "Swap (${SWAP_GB} GB) is smaller than RAM (${RAM_GB} GB)."
    warn "Hibernate may fail if RAM usage exceeds swap size."
    read -rp "Continue anyway? [y/N] " confirm
    [[ "$confirm" =~ ^[Yy] ]] || exit 1
else
    info "Swap size OK: ${SWAP_GB} GB swap >= ${RAM_GB} GB RAM"
fi

# --- Validate delay syntax ---
# systemd accepts formats like: 30min, 1h, 2h, 1h30min, 90s, etc.
if ! systemd-analyze timespan "$DELAY" &>/dev/null; then
    die "Invalid time format: $DELAY (use systemd syntax: 30min, 1h, 2h, etc.)"
fi
DELAY_HUMAN=$(systemd-analyze timespan "$DELAY" 2>/dev/null | grep -oP '(?<=Human: ).*' || echo "$DELAY")
info "Hibernate delay: $DELAY_HUMAN"

# --- Compute resume parameters ---
if [[ "$SWAP_TYPE" == "partition" ]]; then
    RESUME_UUID=$(blkid -s UUID -o value "$SWAP_PATH") || die "Could not get UUID for $SWAP_PATH"
    RESUME_PARAM="resume=UUID=$RESUME_UUID"
    info "Resume parameter: $RESUME_PARAM"
else
    # Swap file: need filesystem UUID + file offset
    FS_DEV=$(df --output=source "$SWAP_PATH" | tail -1)
    RESUME_UUID=$(blkid -s UUID -o value "$FS_DEV") || die "Could not get UUID for $FS_DEV"

    # Get physical offset of first extent
    RESUME_OFFSET=$(filefrag -v "$SWAP_PATH" | awk '/^ *0:/{gsub(/\.\./," "); print $4}')
    [[ -n "$RESUME_OFFSET" ]] || die "Could not determine resume_offset for $SWAP_PATH"

    RESUME_PARAM="resume=UUID=$RESUME_UUID resume_offset=$RESUME_OFFSET"
    info "Resume parameters: $RESUME_PARAM"
fi

# --- Check for existing config ---
if [[ -f "$GRUB_DROPIN" ]]; then
    echo
    warn "Existing hibernate GRUB config found: $GRUB_DROPIN"
    cat "$GRUB_DROPIN"
    read -rp "Overwrite? [y/N] " confirm
    [[ "$confirm" =~ ^[Yy] ]] || exit 1
fi

# --- Write GRUB drop-in ---
info "Writing GRUB drop-in: $GRUB_DROPIN"
mkdir -p "$(dirname "$GRUB_DROPIN")"
cat > "$GRUB_DROPIN" <<EOF
GRUB_CMDLINE_LINUX_DEFAULT="\$GRUB_CMDLINE_LINUX_DEFAULT $RESUME_PARAM"
EOF
cat "$GRUB_DROPIN"

# --- Update GRUB ---
info "Updating GRUB..."
update-grub

# --- Update initramfs (so resume module is included) ---
info "Updating initramfs (this may take a moment)..."
update-initramfs -u -k all

# --- Write sleep.conf drop-in ---
info "Writing sleep config: $SLEEP_DROPIN"
mkdir -p "$(dirname "$SLEEP_DROPIN")"
cat > "$SLEEP_DROPIN" <<EOF
[Sleep]
HibernateDelaySec=$DELAY
EOF

# --- Write logind.conf drop-in ---
info "Writing logind config: $LOGIND_DROPIN"
mkdir -p "$(dirname "$LOGIND_DROPIN")"
cat > "$LOGIND_DROPIN" <<EOF
[Login]
HandleLidSwitch=suspend-then-hibernate
HandleLidSwitchExternalPower=suspend-then-hibernate
EOF

# --- Restart logind ---
info "Restarting logind..."
systemctl restart systemd-logind

# --- Summary ---
echo
echo "========================================"
echo "  Hibernate configuration complete!"
echo "========================================"
echo
echo "  Swap:     $SWAP_PATH ($SWAP_TYPE, ${SWAP_GB} GB)"
echo "  Resume:   $RESUME_PARAM"
echo "  Delay:    $DELAY_HUMAN (s2idle -> hibernate)"
echo
echo "  Files created:"
echo "    $GRUB_DROPIN"
echo "    $SLEEP_DROPIN"
echo "    $LOGIND_DROPIN"
echo
echo "  IMPORTANT: Reboot for GRUB/initramfs changes to take effect."
echo
echo "  After reboot, test with:"
echo "    systemctl hibernate       # test hibernate directly"
echo "    systemctl suspend-then-hibernate  # test STH"
echo
echo "  To remove: sudo $0 --uninstall"
echo
