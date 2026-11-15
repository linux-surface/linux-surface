#!/usr/bin/env bash
#
# surface-pro-5-suspend.sh - set up modern suspend on Surface Pro 5 (2017).
#
# Usage:
#   ./surface-pro-5-suspend.sh               show what the device supports (no changes)
#   sudo ./surface-pro-5-suspend.sh --install
#   sudo ./surface-pro-5-suspend.sh --uninstall
#   sudo ./surface-pro-5-suspend.sh --test   suspend for 60 s and report S0ix residency

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-check}"

SLEEP_CONF=/etc/systemd/sleep.conf.d/50-surface-pro-5.conf
LOGIND_CONF=/etc/systemd/logind.conf.d/50-surface-pro-5.conf
HOOK=/usr/lib/systemd/system-sleep/surface-pro-5-sleep
TMPFILE=/etc/tmpfiles.d/surface-pro-5-mem-sleep.conf
SETTINGS=/etc/surface-pro-5-suspend.conf

ok()   { printf '  \033[32m[ ok ]\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m[warn]\033[0m %s\n' "$*"; }
info() { printf '         %s\n' "$*"; }
hdr()  { printf '\n\033[1m%s\033[0m\n' "$*"; }

need_root() {
    if [ "$(id -u)" != 0 ]; then echo "needs root: sudo $0 $MODE" >&2; exit 1; fi
}

SYSTEMD_VER="$(systemctl --version 2>/dev/null | awk 'NR==1{print $2}' | tr -dc '0-9')"
SYSTEMD_VER="${SYSTEMD_VER:-0}"

# ---------------------------------------------------------------- detection
hdr "Device"
PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null || echo '?')"
SKU="$(cat /sys/class/dmi/id/product_sku 2>/dev/null || echo '?')"
info "$PRODUCT ($SKU), kernel $(uname -r), systemd $SYSTEMD_VER"
case "$PRODUCT:$SKU" in
    "Surface Pro:"Surface_Pro_17*) ok "Surface Pro 5";;
    *) warn "not a Surface Pro 5; the settings are generic, but the Wi-Fi hook targets mwifiex";;
esac

hdr "Suspend modes offered by firmware/kernel"
MEM_SLEEP="$(cat /sys/power/mem_sleep 2>/dev/null || echo '')"
info "/sys/power/mem_sleep: ${MEM_SLEEP:-unavailable}"
HAS_DEEP=0
case " $MEM_SLEEP " in *deep*) HAS_DEEP=1;; esac
if [ "$HAS_DEEP" = 1 ]; then
    ok "S3 'deep' sleep is available and will be used (lowest drain)"
else
    ok "s2idle (S0ix / Modern Standby) is the suspend mode this firmware offers"
    info "That is the same mode Windows uses on the Pro 5; S3 cannot be enabled"
    info "from Linux if the firmware doesn't expose it. This setup makes s2idle"
    info "reach its deepest state and adds hibernation as a backstop."
fi

hdr "Hibernation (needed for suspend-then-hibernate)"
CAN_HIBERNATE=1
if ! grep -qw disk /sys/power/state 2>/dev/null; then
    warn "kernel does not offer hibernation (/sys/power/state has no 'disk')"
    CAN_HIBERNATE=0
fi
LOCKDOWN="$(cat /sys/kernel/security/lockdown 2>/dev/null || true)"
if [[ "$LOCKDOWN" == *"[integrity]"* || "$LOCKDOWN" == *"[confidentiality]"* ]]; then
    warn "kernel lockdown is active ($LOCKDOWN) - hibernation is blocked (often Secure Boot)"
    CAN_HIBERNATE=0
fi
MEM_KB="$(awk '/^MemTotal:/{print $2}' /proc/meminfo)"
SWAP_KB="$(awk '/^SwapTotal:/{print $2}' /proc/meminfo)"
# zram is RAM, it can't hold a hibernation image.
DISK_SWAP_KB="$(awk 'NR>1 && $1 !~ /zram/ {s+=$3} END{print s+0}' /proc/swaps)"
info "RAM $((MEM_KB / 1024)) MiB, swap $((SWAP_KB / 1024)) MiB (on disk: $((DISK_SWAP_KB / 1024)) MiB)"
if [ "$DISK_SWAP_KB" -lt $((MEM_KB * 6 / 10)) ]; then
    warn "not enough disk swap to hold a hibernation image (want >= ~RAM size)"
    CAN_HIBERNATE=0
fi
if grep -q 'resume=' /proc/cmdline; then
    ok "resume= is set on the kernel command line"
elif [ "$SYSTEMD_VER" -ge 255 ]; then
    ok "no resume= but systemd $SYSTEMD_VER can resume via the HibernateLocation EFI variable"
    info "(needs a systemd-based initramfs, e.g. dracut or mkinitcpio with the 'systemd' hook)"
else
    warn "no resume= on the kernel command line; resume from hibernation would fail"
    CAN_HIBERNATE=0
fi
if [ "$CAN_HIBERNATE" = 1 ]; then
    ok "hibernation looks usable -> will use suspend-then-hibernate"
else
    warn "will use plain suspend (fix the items above, then re-run --install)"
fi

hdr "Low-power idle diagnostics"
if [ -r /sys/kernel/debug/pmc_core/slp_s0_residency_usec ]; then
    ok "S0ix residency counter: $(cat /sys/kernel/debug/pmc_core/slp_s0_residency_usec) us"
    info "This number should grow after each s2idle suspend; if it stays at 0,"
    info "something is blocking deep sleep (run with --test)."
else
    info "S0ix counter not readable (needs root, the intel_pmc_core module and debugfs)"
fi
if [ -d /sys/module/mwifiex_pcie ]; then
    info "Marvell Wi-Fi (mwifiex_pcie) loaded - the sleep hook will unload it during suspend"
fi
if [ -e "$HOOK" ] && [ -e "$SLEEP_CONF" ]; then ok "this setup is installed"; fi

# ------------------------------------------------------------------ actions
case "$MODE" in
check)
    echo
    echo "Run 'sudo $0 --install' to apply."
    ;;

--install)
    need_root
    hdr "Installing"
    install -D -m 0644 "$SCRIPT_DIR/sleep.conf.d/50-surface-pro-5.conf" "$SLEEP_CONF"
    ok "$SLEEP_CONF"

    # MemorySleepMode needs systemd 256+. On older systemd, choose the mode
    # at boot through tmpfiles instead.
    if [ "$SYSTEMD_VER" -lt 256 ]; then
        if [ "$HAS_DEEP" = 1 ]; then
            echo 'w /sys/power/mem_sleep - - - - deep' > "$TMPFILE"
        else
            echo 'w /sys/power/mem_sleep - - - - s2idle' > "$TMPFILE"
        fi
        systemd-tmpfiles --create "$TMPFILE"
        ok "$TMPFILE (systemd < 256 fallback)"
    fi

    install -D -m 0644 "$SCRIPT_DIR/logind.conf.d/50-surface-pro-5.conf" "$LOGIND_CONF"
    if [ "$CAN_HIBERNATE" = 0 ]; then
        sed -i 's/suspend-then-hibernate/suspend/' "$LOGIND_CONF"
    fi
    ok "$LOGIND_CONF ($(grep -m1 HandleLidSwitch= "$LOGIND_CONF"))"

    install -D -m 0755 "$SCRIPT_DIR/system-sleep/surface-pro-5-sleep" "$HOOK"
    [ -e "$SETTINGS" ] || printf '# See %s\nUNLOAD_WIFI=yes\n' "$HOOK" > "$SETTINGS"
    ok "$HOOK (settings: $SETTINGS)"

    systemctl daemon-reload
    info "logind settings apply after reboot (restarting logind now would end your session)."
    ;;

--uninstall)
    need_root
    rm -f "$SLEEP_CONF" "$LOGIND_CONF" "$HOOK" "$TMPFILE" "$SETTINGS"
    systemctl daemon-reload
    ok "removed; reboot to restore defaults"
    ;;

--test)
    need_root
    RES=/sys/kernel/debug/pmc_core/slp_s0_residency_usec
    before="$(cat "$RES" 2>/dev/null || echo NA)"
    hdr "Suspending for 60 s (mode: $(cat /sys/power/mem_sleep))"
    if command -v rtcwake >/dev/null 2>&1; then
        rtcwake -m no -s 60 >/dev/null && systemctl suspend
        sleep 15   # monotonic: this finishes ~15 s after resume
    else
        echo "rtcwake (util-linux) not found" >&2; exit 1
    fi
    after="$(cat "$RES" 2>/dev/null || echo NA)"
    info "S0ix residency before: $before  after: $after"
    if [ "$before" != NA ] && [ "$after" != NA ]; then
        if [ "$after" -gt "$before" ]; then
            ok "reached S0ix for $(( (after - before) / 1000000 )) s - deep low-power idle works"
        elif [ "$HAS_DEEP" = 1 ] && grep -q '\[deep\]' /sys/power/mem_sleep; then
            ok "used S3 deep sleep (the S0ix counter doesn't apply)"
        else
            warn "S0ix not reached; check: sudo cat /sys/kernel/debug/pmc_core/substate_requirements"
            info "and journalctl -b | grep -iE 'PM:|s2idle|wake' for what kept it awake"
        fi
    fi
    ;;

*)
    sed -n '3,9p' "$0"; exit 1;;
esac
