#!/usr/bin/env bash
#
# surface-camera-setup.sh - diagnose and set up the IPU3 cameras on
# Surface Pro 5 (2017), Surface Pro 6 and Surface Book 2.
#
# Usage:
#   ./surface-camera-setup.sh            check everything, change nothing
#   sudo ./surface-camera-setup.sh --fix install packages + config, reload modules
#
# The cameras need the whole stack below. If any layer is missing, the
# cameras silently stop working, which is what typically happens when moving
# from a distro with the linux-surface kernel to one with a stock kernel:
#
#   kernel:    ipu3-cio2, ipu3-imgu, ipu-bridge, int3472 (TPS68470 PMIC),
#              ov5693 (front), ov8865 (rear), ov7251 (IR)
#              + linux-surface "cameras" patches (ACPI probe ordering,
#                IOMMU passthrough for the IPU, TPS68470 I2C daisy chain)
#   firmware:  intel/irci_irci_ecr-master_20161208_0213_20170112_1500.bin
#   userspace: libcamera with the ipu3 pipeline + IPA, PipeWire libcamera
#              plugin, WirePlumber

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

ok()   { printf '  \033[32m[ ok ]\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m[warn]\033[0m %s\n' "$*"; WARNINGS=$((WARNINGS + 1)); }
bad()  { printf '  \033[31m[FAIL]\033[0m %s\n' "$*"; FAILURES=$((FAILURES + 1)); }
info() { printf '         %s\n' "$*"; }
hdr()  { printf '\n\033[1m%s\033[0m\n' "$*"; }

WARNINGS=0
FAILURES=0
NEED_SURFACE_KERNEL=0
NEED_IOMMU_PT=0

if [ "$FIX" = 1 ] && [ "$(id -u)" != 0 ]; then
    echo "--fix needs root: sudo $0 --fix" >&2
    exit 1
fi

# The desktop user, for per-user config (WirePlumber).
TARGET_USER="${SUDO_USER:-$(id -un)}"
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"

detect_distro() {
    if command -v pacman >/dev/null 2>&1; then echo arch
    elif command -v apt-get >/dev/null 2>&1; then echo debian
    elif command -v dnf >/dev/null 2>&1; then echo fedora
    else echo unknown
    fi
}
DISTRO="$(detect_distro)"

# --------------------------------------------------------------------------
hdr "Device"
VENDOR="$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || echo '?')"
PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null || echo '?')"
SKU="$(cat /sys/class/dmi/id/product_sku 2>/dev/null || echo '?')"
info "$VENDOR / $PRODUCT / $SKU"
case "$PRODUCT:$SKU" in
    "Surface Pro:"*Surface_Pro_17*|"Surface Pro 6:"*|"Surface Book 2:"*)
        ok "IPU3 Surface device (OV5693 front, OV8865 rear, OV7251 IR)";;
    *)
        warn "This script targets Surface Pro 5/6 and Book 2; continuing anyway";;
esac
info "distro family: $DISTRO"

# --------------------------------------------------------------------------
hdr "Kernel"
KREL="$(uname -r)"
info "running kernel: $KREL"
if [[ "$KREL" == *surface* ]]; then
    ok "linux-surface kernel (includes the camera patches)"
else
    bad "not a linux-surface kernel"
    info "Stock kernels lack the probe-ordering and IOMMU fixes the Surface"
    info "Pro 5 cameras need; two of those are in built-in kernel code, so"
    info "no add-on module can supply them. This is the most likely reason"
    info "the cameras worked on Mint (with linux-surface) but not now."
    NEED_SURFACE_KERNEL=1
fi

MODULES="ipu3_cio2 ipu3_imgu ipu_bridge intel_skl_int3472_tps68470 tps68470-regulator clk-tps68470 ov5693 ov8865 ov7251"
PRESENT_MODULES=""
for m in $MODULES; do
    if modinfo "$m" >/dev/null 2>&1; then
        PRESENT_MODULES="$PRESENT_MODULES $m"
    else
        case "$m" in
            ov7251) warn "module $m missing (IR camera only, not needed for normal use)";;
            *)      bad "module $m is not built for this kernel";;
        esac
    fi
done
[ -n "$PRESENT_MODULES" ] && ok "modules available:$PRESENT_MODULES"

if grep -qE '(^| )(iommu=pt|intel_iommu=off)( |$)' /proc/cmdline; then
    ok "IOMMU passthrough/off set on kernel command line"
elif [ -d /sys/class/iommu ] && [ -n "$(ls -A /sys/class/iommu 2>/dev/null)" ] \
     && [[ "$KREL" != *surface* ]]; then
    bad "Intel IOMMU is active and this kernel has no IPU passthrough patch"
    info "The IPU3 DMA gets blocked (dmesg: 'DMAR: [DMA Read] ... 00:05.0')."
    NEED_IOMMU_PT=1
else
    ok "IOMMU not blocking the IPU"
fi

# --------------------------------------------------------------------------
hdr "Firmware"
FW_FOUND=""
for f in /lib/firmware/intel/irci_irci_ecr-master_20161208_0213_20170112_1500.bin \
         /usr/lib/firmware/intel/irci_irci_ecr-master_20161208_0213_20170112_1500.bin \
         /lib/firmware/intel/ipu3-fw.bin /usr/lib/firmware/intel/ipu3-fw.bin \
         /usr/lib/firmware/intel/ipu/irci_irci_ecr-master_20161208_0213_20170112_1500.bin; do
    for ext in "" .zst .xz; do
        [ -e "$f$ext" ] && FW_FOUND="$f$ext" && break 2
    done
done
if [ -n "$FW_FOUND" ]; then
    ok "ImgU firmware: $FW_FOUND"
else
    bad "IPU3 ImgU firmware not found"
    case "$DISTRO" in
        arch)   info "install: pacman -S linux-firmware-intel (or linux-firmware)";;
        debian) info "install: apt install firmware-misc-nonfree (Debian) / linux-firmware (Ubuntu/Mint)";;
        fedora) info "install: dnf install linux-firmware";;
    esac
fi

# --------------------------------------------------------------------------
hdr "Loaded drivers and sensors"
for m in ipu3_cio2 ipu3_imgu; do
    if [ -d "/sys/module/$m" ]; then ok "$m loaded"; else warn "$m not loaded"; fi
done
SENSORS=""
for d in /sys/bus/i2c/devices/*; do
    [ -e "$d/name" ] || continue
    drv="$(basename "$(readlink -f "$d/driver" 2>/dev/null)" 2>/dev/null)"
    case "$(cat "$d/name")" in
        *OVTI5693*|*INT33BE*|ov5693*) SENSORS="$SENSORS front(ov5693:${drv:-unbound})";;
        *OVTI8865*|*INT347A*|ov8865*) SENSORS="$SENSORS rear(ov8865:${drv:-unbound})";;
        *OVTI7251*|*INT347E*|ov7251*) SENSORS="$SENSORS ir(ov7251:${drv:-unbound})";;
    esac
done
if [ -n "$SENSORS" ]; then
    info "sensors on I2C:$SENSORS"
    if [[ "$SENSORS" == *unbound* ]]; then
        bad "some sensors have no driver bound (probe failed or ran too early)"
    else
        ok "all detected sensors are bound"
    fi
else
    warn "no camera sensors enumerated on I2C (expected on stock kernels without the ACPI patch)"
fi
if ls /dev/media* >/dev/null 2>&1; then
    ok "media devices: $(echo /dev/media*)"
else
    warn "no /dev/media* devices"
fi
if [ "$(id -u)" = 0 ] && command -v dmesg >/dev/null 2>&1; then
    ERRS="$(dmesg 2>/dev/null | grep -iE 'ov5693|ov8865|ov7251|int3472|tps68470|ipu3|cio2|DMAR.*00:05\.0' | grep -iE 'fail|error|fault|defer|unable' | tail -n 8)"
    if [ -n "$ERRS" ]; then
        warn "relevant kernel errors:"
        printf '%s\n' "$ERRS" | sed 's/^/           /'
    fi
fi

# --------------------------------------------------------------------------
hdr "Userspace (libcamera / PipeWire)"
LIBCAMERA_IPU3=""
for p in /usr/lib/libcamera /usr/lib64/libcamera /usr/lib/*/libcamera \
         /usr/lib/libcamera/ipa /usr/lib64/libcamera/ipa /usr/lib/*/libcamera/ipa; do
    ls "$p"/ipa_ipu3*.so >/dev/null 2>&1 && LIBCAMERA_IPU3="$p" && break
done
if [ -n "$LIBCAMERA_IPU3" ]; then
    ok "libcamera IPU3 IPA found in $LIBCAMERA_IPU3"
else
    bad "libcamera IPU3 IPA module not found (libcamera or libcamera-ipa missing)"
fi
SPA=""
for p in /usr/lib/spa-0.2/libcamera /usr/lib64/spa-0.2/libcamera /usr/lib/*/spa-0.2/libcamera; do
    [ -d "$p" ] && SPA="$p" && break
done
if [ -n "$SPA" ]; then ok "PipeWire libcamera plugin: $SPA"
else bad "PipeWire libcamera plugin missing (apps like Firefox, Chrome, Snapshot can't see the cameras)"; fi

if command -v cam >/dev/null 2>&1; then
    CAMS="$(cam -l 2>/dev/null | grep -E '^[0-9]+:')"
    if [ -n "$CAMS" ]; then
        ok "libcamera sees:"
        printf '%s\n' "$CAMS" | sed 's/^/           /'
    else
        bad "libcamera ('cam -l') lists no cameras"
    fi
else
    warn "'cam' tool not installed (libcamera-tools) - can't test libcamera directly"
fi

WP_CONF="$TARGET_HOME/.config/wireplumber/wireplumber.conf.d/51-surface-ipu3-cameras.conf"
if [ -e "$WP_CONF" ]; then ok "WirePlumber camera rules installed"
else warn "WirePlumber camera rules not installed (raw ipu3 nodes may confuse apps)"; fi

# --------------------------------------------------------------------------
if [ "$FIX" = 1 ]; then
    hdr "Applying fixes"

    case "$DISTRO" in
        arch)
            pacman -S --needed --noconfirm libcamera libcamera-ipa libcamera-tools \
                pipewire-libcamera gst-plugin-libcamera v4l-utils \
                || warn "package install failed (names can differ on derivatives)";;
        debian)
            apt-get install -y libcamera-tools libcamera-ipa libspa-0.2-libcamera \
                gstreamer1.0-libcamera v4l-utils \
                || warn "package install failed";;
        fedora)
            dnf install -y libcamera libcamera-tools libcamera-ipa \
                pipewire-plugin-libcamera gstreamer1-plugin-libcamera v4l-utils \
                || warn "package install failed";;
        *)  warn "unknown package manager; install libcamera (+ipa, tools) and the PipeWire libcamera plugin yourself";;
    esac

    # modprobe softdeps, only naming modules this kernel has.
    pre=""
    for m in intel_skl_int3472_tps68470 tps68470-regulator clk-tps68470; do
        modinfo "$m" >/dev/null 2>&1 && pre="$pre $m"
    done
    {
        echo "# Generated by surface-camera-setup.sh for $KREL"
        echo "# See contrib/surface-ipu3-cameras/modprobe.d/surface-ipu3-cameras.conf"
        if [ -n "$pre" ]; then
            for s in ov5693 ov8865 ov7251; do
                modinfo "$s" >/dev/null 2>&1 && echo "softdep $s pre:$pre"
            done
        fi
        modinfo ipu_bridge >/dev/null 2>&1 && echo "softdep ipu3_cio2 pre: ipu_bridge post: ipu3_imgu"
    } > /etc/modprobe.d/surface-ipu3-cameras.conf
    ok "wrote /etc/modprobe.d/surface-ipu3-cameras.conf"

    # Make sure the sensors are loaded at boot even if udev races.
    printf '%s\n' ipu3_cio2 ipu3_imgu > /etc/modules-load.d/surface-ipu3-cameras.conf
    ok "wrote /etc/modules-load.d/surface-ipu3-cameras.conf"

    # Reload in the right order: PMIC first, then sensors, then CIO2/ImgU.
    for m in ipu3_imgu ipu3_cio2 ov7251 ov8865 ov5693; do modprobe -r "$m" 2>/dev/null; done
    for m in intel_skl_int3472_tps68470 tps68470-regulator clk-tps68470 \
             ov5693 ov8865 ov7251 ipu_bridge ipu3_cio2 ipu3_imgu; do
        modprobe "$m" 2>/dev/null
    done
    ok "reloaded camera modules"

    if [ -n "$TARGET_HOME" ] && [ -d "$TARGET_HOME" ]; then
        install -D -m 0644 -o "$TARGET_USER" -g "$(id -gn "$TARGET_USER")" \
            "$SCRIPT_DIR/wireplumber/51-surface-ipu3-cameras.conf" "$WP_CONF"
        chown -R "$TARGET_USER:$(id -gn "$TARGET_USER")" "$TARGET_HOME/.config/wireplumber"
        ok "installed WirePlumber rules for $TARGET_USER"
        info "then run as $TARGET_USER:  systemctl --user restart wireplumber pipewire"
    fi
fi

# --------------------------------------------------------------------------
hdr "What to do next"
if [ "$NEED_SURFACE_KERNEL" = 1 ]; then
    case "$DISTRO" in
        arch)
            cat <<'EOF'
  Install the linux-surface kernel (this is what fixes the cameras):

    curl -s https://raw.githubusercontent.com/linux-surface/linux-surface/master/pkg/keys/surface.asc \
        | sudo pacman-key --add -
    sudo pacman-key --finger 56C464BAAC421453
    sudo pacman-key --lsign-key 56C464BAAC421453
    printf '\n[linux-surface]\nServer = https://pkg.surfacelinux.com/arch/\n' \
        | sudo tee -a /etc/pacman.conf
    sudo pacman -Syu linux-surface linux-surface-headers iptsd

  Then make sure your bootloader boots it (EndeavourOS/systemd-boot:
  sudo reinstall-kernels; GRUB: sudo grub-mkconfig -o /boot/grub/grub.cfg),
  reboot, and run this script again with --fix.
EOF
            ;;
        *)
            echo "  Install the linux-surface kernel: https://github.com/linux-surface/linux-surface/wiki/Installation-and-Setup"
            ;;
    esac
fi
if [ "$NEED_IOMMU_PT" = 1 ]; then
    cat <<'EOF'

  If you must stay on a stock kernel, add  iommu=pt  to the kernel command
  line (systemd-boot: /etc/kernel/cmdline then sudo reinstall-kernels;
  GRUB: GRUB_CMDLINE_LINUX_DEFAULT in /etc/default/grub then grub-mkconfig).
  This lets the IPU3 do DMA again. It does not replace the other patches.
EOF
fi
cat <<'EOF'

  Test:      cam -l                      (should list 2-3 cameras)
             cam -c 1 --capture=10        (grab frames from camera 1)
             gst-launch-1.0 libcamerasrc ! videoconvert ! autovideosink
  Apps:      GNOME Snapshot / Cheese work through PipeWire.
             Firefox: about:config -> media.webrtc.camera.allow-pipewire = true
             Chromium/Chrome: chrome://flags -> "PipeWire Camera support" = Enabled
EOF

printf '\n%d failure(s), %d warning(s).\n' "$FAILURES" "$WARNINGS"
[ "$FAILURES" = 0 ]
