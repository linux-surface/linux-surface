#!/usr/bin/env bash
#
# surface-camera-setup.sh - diagnose and set up the IPU3 cameras (front,
# rear and IR) on Surface Pro 4/5/6 and Surface Book 1/2.
#
# Usage:
#   ./surface-camera-setup.sh             check everything, change nothing
#   sudo ./surface-camera-setup.sh --fix  install packages, config and the
#                                         surface-camd camera driver
#   sudo ./surface-camera-setup.sh --report
#                                         write surface-camera-report.txt with
#                                         all camera logs, for bug reports
#
# Stack:
#   kernel:    ipu3-cio2, ipu-bridge, int3472 (discrete), ov5693 (front),
#              ov8865 (rear), ov7251 (IR) + linux-surface "cameras" patches
#   driver:    surface-camd (this directory): raw sensor -> processed image ->
#              v4l2loopback webcam. Works without libcamera, supports IR.
#   optional:  libcamera (ipu3 pipeline) + PipeWire plugin, front/rear only

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-check}"

ok()   { printf '  \033[32m[ ok ]\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m[warn]\033[0m %s\n' "$*"; WARNINGS=$((WARNINGS + 1)); }
bad()  { printf '  \033[31m[FAIL]\033[0m %s\n' "$*"; FAILURES=$((FAILURES + 1)); }
info() { printf '         %s\n' "$*"; }
hdr()  { printf '\n\033[1m%s\033[0m\n' "$*"; }

WARNINGS=0
FAILURES=0
NEED_IOMMU_PT=0
SENSOR_PROBLEM=0

case "$MODE" in
    check|--fix|--report) ;;
    *) sed -n '3,19p' "$0"; exit 1;;
esac
if [ "$MODE" != check ] && [ "$(id -u)" != 0 ]; then
    echo "$MODE needs root: sudo $0 $MODE" >&2
    exit 1
fi

TARGET_USER="${SUDO_USER:-$(id -un)}"
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"

if command -v pacman >/dev/null 2>&1; then DISTRO=arch
elif command -v apt-get >/dev/null 2>&1; then DISTRO=debian
elif command -v dnf >/dev/null 2>&1; then DISTRO=fedora
else DISTRO=unknown
fi

# ------------------------------------------------------------------ report
if [ "$MODE" = --report ]; then
    OUT="$TARGET_HOME/surface-camera-report.txt"
    {
        echo "== system"; uname -a
        cat /sys/class/dmi/id/{sys_vendor,product_name,product_sku,bios_version} 2>/dev/null
        cat /etc/os-release 2>/dev/null | head -4
        echo; echo "== cmdline"; cat /proc/cmdline
        echo; echo "== modules"
        for m in ipu3_cio2 ipu3_imgu ipu_bridge intel_skl_int3472_discrete intel_skl_int3472_tps68470 \
                 ov5693 ov8865 ov7251 dw9714 dw9719 v4l2loopback; do
            printf '%-30s loaded=%s file=%s\n' "$m" "$([ -d /sys/module/$m ] && echo yes || echo no)" \
                "$(modinfo -n "$m" 2>/dev/null || echo MISSING)"
        done
        echo; echo "== ACPI camera devices"
        for d in /sys/bus/acpi/devices/{INT3472,INT33BE,INT347A,INT347E,OVTI}*; do
            [ -e "$d" ] || continue
            printf '%s status=%s path=%s\n' "$(basename "$d")" "$(cat "$d/status" 2>/dev/null)" "$(cat "$d/path" 2>/dev/null)"
        done
        echo; echo "== I2C devices"
        for d in /sys/bus/i2c/devices/*; do
            [ -e "$d/name" ] && printf '%s name=%s driver=%s\n' "$(basename "$d")" "$(cat "$d/name")" \
                "$(basename "$(readlink -f "$d/driver" 2>/dev/null)" 2>/dev/null)"
        done
        echo; echo "== LEDs"; ls /sys/class/leds 2>/dev/null
        echo; echo "== kernel log"
        dmesg | grep -iE 'ov5693|ov8865|ov7251|int3472|tps68470|ipu3|cio2|ipu.bridge|DMAR|dw97|v4l2loopback|INT33BE|INT347'
        for m in /dev/media*; do
            [ -e "$m" ] || continue
            echo; echo "== media-ctl -p -d $m"
            media-ctl -p -d "$m" 2>&1 || echo "(media-ctl not installed: v4l-utils)"
        done
        echo; echo "== v4l2-ctl --list-devices"; v4l2-ctl --list-devices 2>&1
        echo; echo "== cam -l"; cam -l 2>&1
        echo; echo "== surface-camd"
        for i in front rear ir; do systemctl status --no-pager -n 15 "surface-camd@$i" 2>&1; done
        echo; echo "== packages"
        case "$DISTRO" in
            arch) pacman -Q 2>/dev/null | grep -iE 'linux-surface|libcamera|pipewire|wireplumber|v4l2loopback|firmware';;
            debian) dpkg -l 2>/dev/null | grep -iE 'linux-image-surface|libcamera|pipewire|wireplumber|v4l2loopback' | awk '{print $2, $3}';;
            fedora) rpm -qa 2>/dev/null | grep -iE 'kernel-surface|libcamera|pipewire|wireplumber|v4l2loopback';;
        esac
    } > "$OUT" 2>&1
    chown "$TARGET_USER" "$OUT" 2>/dev/null
    echo "Wrote $OUT"
    exit 0
fi

# ------------------------------------------------------------------ device
hdr "Device"
PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null || echo '?')"
SKU="$(cat /sys/class/dmi/id/product_sku 2>/dev/null || echo '?')"
info "$PRODUCT ($SKU), distro family: $DISTRO"
case "$PRODUCT:$SKU" in
    "Surface Pro:"Surface_Pro_17*|"Surface Pro 4:"*|"Surface Pro 6:"*|"Surface Book:"*|"Surface Book 2:"*)
        ok "IPU3 Surface device (OV5693 front, OV8865 rear, OV7251 IR)";;
    *)
        warn "This script targets Surface Pro 4/5/6 and Book 1/2; continuing anyway";;
esac

# ------------------------------------------------------------------ kernel
hdr "Kernel"
KREL="$(uname -r)"
if [[ "$KREL" == *surface* ]]; then
    ok "linux-surface kernel $KREL"
else
    warn "kernel $KREL is not a linux-surface kernel (it lacks the camera patches)"
fi

for m in ipu3_cio2 ipu_bridge intel_skl_int3472_discrete ov5693 ov8865 ov7251; do
    modinfo "$m" >/dev/null 2>&1 || bad "module $m is not built for this kernel"
done

if grep -qE '(^| )(iommu=pt|intel_iommu=off)( |$)' /proc/cmdline; then
    ok "IOMMU passthrough/off on the kernel command line"
elif [ -n "$(ls -A /sys/class/iommu 2>/dev/null)" ] && [[ "$KREL" != *surface* ]]; then
    bad "Intel IOMMU is active and this kernel lacks the IPU passthrough patch"
    NEED_IOMMU_PT=1
fi

# ------------------------------------------------------------------ sensors
hdr "Sensors"
for spec in "front:ov5693:INT33BE" "rear:ov8865:INT347A" "ir:ov7251:INT347E"; do
    IFS=: read -r cam drv hid <<<"$spec"
    acpi=""
    for d in /sys/bus/acpi/devices/"$hid":*; do [ -e "$d" ] && acpi="$d" && break; done
    i2c=""
    for d in /sys/bus/i2c/devices/*"$hid"*; do [ -e "$d" ] && i2c="$d" && break; done
    if [ -z "$acpi" ]; then
        warn "$cam ($drv): no ACPI device $hid - not present on this model?"
        continue
    fi
    if [ -z "$i2c" ]; then
        bad "$cam ($drv): ACPI device exists but no I2C device was created"
        info "the INT3472 driver didn't finish (power/clock setup); see dmesg | grep int3472"
        SENSOR_PROBLEM=1
        continue
    fi
    bound="$(basename "$(readlink -f "$i2c/driver" 2>/dev/null)" 2>/dev/null)"
    if [ -n "$bound" ]; then
        ok "$cam ($drv): driver bound ($(basename "$i2c"))"
    else
        bad "$cam ($drv): $(basename "$i2c") has no driver bound (probe failed)"
        SENSOR_PROBLEM=1
    fi
done
IR_LED=""
for l in /sys/class/leds/*ir_flood*; do [ -e "$l" ] && IR_LED="$(basename "$l")" && break; done
if [ -n "$IR_LED" ]; then
    ok "IR flood LED: $IR_LED"
else
    warn "no IR flood LED exposed by INT3472 (IR images will be dark in the dark)"
fi
if [ "$(id -u)" = 0 ]; then
    ERRS="$(dmesg 2>/dev/null | grep -iE 'ov5693|ov8865|ov7251|int3472|ipu3|cio2|ipu.bridge|DMAR.*00:05\.0' \
            | grep -iE 'fail|error|fault|unable|timeout|no board' | tail -n 8)"
    if [ -n "$ERRS" ]; then
        warn "camera errors in the kernel log:"
        printf '%s\n' "$ERRS" | sed 's/^/           /'
    fi
fi

# ------------------------------------------------------------- surface-camd
hdr "surface-camd (camera driver)"
CAMD="$(command -v surface-camd || true)"
if [ -n "$CAMD" ]; then ok "installed: $CAMD"; else warn "not installed (run with --fix)"; fi
if modinfo v4l2loopback >/dev/null 2>&1; then
    ok "v4l2loopback available"
else
    warn "v4l2loopback module not available"
fi
for i in front rear ir; do
    st="$(systemctl is-active "surface-camd@$i" 2>/dev/null)"
    case "$st" in
        active) ok "surface-camd@$i running";;
        *) [ -n "$CAMD" ] && warn "surface-camd@$i: ${st:-not running} (journalctl -u surface-camd@$i)";;
    esac
done

# -------------------------------------------------------------- libcamera
hdr "libcamera / PipeWire (optional for front/rear)"
if command -v cam >/dev/null 2>&1; then
    CAMS="$(cam -l 2>/dev/null | grep -E '^[0-9]+:')"
    if [ -n "$CAMS" ]; then ok "libcamera sees:"; printf '%s\n' "$CAMS" | sed 's/^/           /'
    else info "libcamera lists no cameras - surface-camd does not need it"; fi
else
    info "libcamera tools not installed - surface-camd does not need them"
fi

# -------------------------------------------------------------------- fix
if [ "$MODE" = --fix ]; then
    hdr "Applying fixes"

    case "$DISTRO" in
        arch)
            HDRS=linux-headers
            pacman -Q linux-surface >/dev/null 2>&1 && HDRS=linux-surface-headers
            pacman -S --needed --noconfirm base-devel v4l-utils v4l2loopback-dkms "$HDRS" \
                || warn "package install failed";;
        debian)
            apt-get install -y build-essential v4l-utils v4l2loopback-dkms \
                || warn "package install failed";;
        fedora)
            dnf install -y gcc make v4l-utils v4l2loopback \
                || warn "package install failed (v4l2loopback is in RPM Fusion)";;
        *) warn "install gcc, make, v4l-utils and v4l2loopback yourself";;
    esac

    # Load-order soft dependencies, only for modules this kernel has.
    {
        echo "# Generated by surface-camera-setup.sh for $KREL"
        if modinfo intel_skl_int3472_discrete >/dev/null 2>&1; then
            for s in ov5693 ov8865 ov7251; do
                modinfo "$s" >/dev/null 2>&1 && echo "softdep $s pre: intel_skl_int3472_discrete"
            done
        fi
        modinfo ipu_bridge >/dev/null 2>&1 && echo "softdep ipu3_cio2 pre: ipu_bridge post: ipu3_imgu"
    } > /etc/modprobe.d/surface-ipu3-cameras.conf
    printf '%s\n' ipu3_cio2 > /etc/modules-load.d/surface-ipu3-cameras.conf
    ok "module load order configured"

    # Build and install the driver.
    if make -C "$SCRIPT_DIR/surface-camd" clean all test >/dev/null; then
        make -C "$SCRIPT_DIR/surface-camd" install PREFIX=/usr/local >/dev/null
        ok "surface-camd built, tested and installed"
    else
        bad "building surface-camd failed (make -C $SCRIPT_DIR/surface-camd)"
    fi

    # Reload camera modules in dependency order.
    for m in ipu3_imgu ipu3_cio2 ov7251 ov8865 ov5693; do modprobe -r "$m" 2>/dev/null; done
    for m in intel_skl_int3472_discrete ov5693 ov8865 ov7251 ipu_bridge ipu3_cio2; do
        modprobe "$m" 2>/dev/null
    done
    modprobe -r v4l2loopback 2>/dev/null
    modprobe v4l2loopback || warn "v4l2loopback did not load (reboot after a kernel update, or check DKMS)"
    ok "modules reloaded"

    systemctl daemon-reload
    for i in front rear ir; do
        systemctl enable "surface-camd@$i" >/dev/null 2>&1
        systemctl restart "surface-camd@$i"
    done
    sleep 3
    for i in front rear ir; do
        if systemctl is-active -q "surface-camd@$i"; then ok "surface-camd@$i running"
        else bad "surface-camd@$i failed: journalctl -u surface-camd@$i"; fi
    done

    if [ -n "$TARGET_HOME" ] && [ -d "$TARGET_HOME" ]; then
        WPD="$TARGET_HOME/.config/wireplumber/wireplumber.conf.d"
        install -d "$WPD"
        install -m 0644 "$SCRIPT_DIR/wireplumber/51-surface-ipu3-cameras.conf" "$WPD/"
        chown -R "$TARGET_USER:$(id -gn "$TARGET_USER")" "$TARGET_HOME/.config/wireplumber"
        ok "WirePlumber rules installed; as $TARGET_USER run: systemctl --user restart wireplumber"
    fi
fi

# ---------------------------------------------------------------- summary
hdr "Next steps"
if [ "$SENSOR_PROBLEM" = 1 ]; then
    cat <<'EOF'
  A sensor did not probe in the kernel, so no userspace driver can reach it
  yet. Run:  sudo ./surface-camera-setup.sh --report
  and share ~/surface-camera-report.txt - it has what's needed to write a
  kernel fix for your exact failure.
EOF
fi
if [ "$NEED_IOMMU_PT" = 1 ]; then
    echo "  Add  iommu=pt  to the kernel command line, or boot the linux-surface kernel."
fi
cat <<'EOF'
  Cameras (after --fix):  /dev/video50 front, /dev/video51 rear, /dev/video52 IR
  Test:                   ffplay /dev/video50     (or open it in any webcam app)
  Firefox/Chrome see them as "Surface Front Camera" etc.
EOF

printf '\n%d failure(s), %d warning(s).\n' "$FAILURES" "$WARNINGS"
[ "$FAILURES" = 0 ]
