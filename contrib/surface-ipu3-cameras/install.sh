#!/usr/bin/env bash
#
# install.sh - build surface-camd as a distro package, install it, and start
# the front, rear and IR camera services.
#
# Usage (run as your normal user; it uses sudo where needed):
#   ./install.sh                     build + install + start all cameras
#   ./install.sh --cameras "front ir"   only start these cameras
#   ./install.sh --build-only        just build the package into ./build
#   ./install.sh --no-start          install, but don't enable/start services
#   ./install.sh --skip-deps         don't install build tools/v4l2loopback/headers
#   ./install.sh --uninstall         stop services and remove the package
#
# Packages:  Arch-based   -> makepkg  -> build/arch/surface-camd-*.pkg.tar.*
#            Debian-based -> dpkg-deb -> build/deb/surface-camd_*.deb
#            Fedora-based -> rpmbuild -> build/rpm/RPMS/*/surface-camd-*.rpm
#            anything else -> plain "make install" (no package)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/surface-camd"
BUILD="$SCRIPT_DIR/build"
VERSION="$(cat "$SRC/VERSION")"

CAMERAS="front rear ir"
BUILD_ONLY=0
START=1
UNINSTALL=0
SKIP_DEPS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --cameras) CAMERAS="$2"; shift 2;;
        --build-only) BUILD_ONLY=1; shift;;
        --no-start) START=0; shift;;
        --uninstall) UNINSTALL=1; shift;;
        --skip-deps) SKIP_DEPS=1; shift;;
        -h|--help) sed -n '3,18p' "$0"; exit 0;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1;;
    esac
done
for c in $CAMERAS; do
    case "$c" in front|rear|ir) ;; *) echo "unknown camera: $c" >&2; exit 1;; esac
done

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m[ ok ]\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m[warn]\033[0m %s\n' "$*"; }
fail() { printf '  \033[31m[FAIL]\033[0m %s\n' "$*" >&2; exit 1; }

# Privileges: build as a normal user, install with sudo.
if [ "$(id -u)" = 0 ]; then
    SUDO=""
    BUILD_USER="${SUDO_USER:-}"
else
    command -v sudo >/dev/null || fail "sudo is required"
    SUDO="sudo"
    BUILD_USER="$(id -un)"
fi
as_user() {
    if [ "$(id -u)" = 0 ] && [ -n "$BUILD_USER" ] && [ "$BUILD_USER" != root ]; then
        runuser -u "$BUILD_USER" -- "$@"
    else
        "$@"
    fi
}

if command -v pacman >/dev/null 2>&1; then DISTRO=arch
elif command -v apt-get >/dev/null 2>&1; then DISTRO=debian
elif command -v dnf >/dev/null 2>&1; then DISTRO=fedora
else DISTRO=other
fi

stop_services() {
    for c in front rear ir; do
        $SUDO systemctl disable --now "surface-camd@$c" >/dev/null 2>&1 || true
    done
}

# --------------------------------------------------------------- uninstall
if [ "$UNINSTALL" = 1 ]; then
    step "Removing surface-camd"
    stop_services
    case "$DISTRO" in
        arch)   $SUDO pacman -R --noconfirm surface-camd;;
        debian) $SUDO apt-get remove -y surface-camd;;
        fedora) $SUDO dnf remove -y surface-camd;;
        other)  (cd "$SRC" && $SUDO rm -f /usr/local/bin/surface-camd \
                    /etc/systemd/system/surface-camd@.service \
                    /etc/modprobe.d/v4l2loopback-surface.conf \
                    /etc/modules-load.d/v4l2loopback-surface.conf);;
    esac
    $SUDO systemctl daemon-reload
    ok "removed"
    exit 0
fi

if [ "$DISTRO" = arch ] && [ "$(id -u)" = 0 ] && { [ -z "$BUILD_USER" ] || [ "$BUILD_USER" = root ]; }; then
    fail "makepkg can't run as root. Run ./install.sh as your normal user (it uses sudo)."
fi

echo "surface-camd $VERSION, distro family: $DISTRO, kernel $(uname -r)"

# ------------------------------------------------------------ dependencies
step "Installing build tools, v4l2loopback and kernel headers"
KREL="$(uname -r)"
[ "$SKIP_DEPS" = 1 ] && DEPS=skip || DEPS="$DISTRO"
case "$DEPS" in
    skip) ok "skipped (--skip-deps)";;
    arch)
        if pacman -Q linux-surface >/dev/null 2>&1 && [[ "$KREL" == *surface* ]]; then
            HDRS=linux-surface-headers
        elif [[ "$KREL" == *lts* ]]; then HDRS=linux-lts-headers
        elif [[ "$KREL" == *zen* ]]; then HDRS=linux-zen-headers
        else HDRS=linux-headers
        fi
        $SUDO pacman -S --needed --noconfirm base-devel v4l2loopback-dkms v4l-utils "$HDRS"
        ;;
    debian)
        $SUDO apt-get update -q || warn "apt-get update failed; continuing"
        $SUDO apt-get install -y build-essential dpkg-dev v4l2loopback-dkms v4l-utils
        $SUDO apt-get install -y "linux-headers-$KREL" 2>/dev/null \
            || $SUDO apt-get install -y linux-headers-surface 2>/dev/null \
            || warn "no headers package for $KREL found; v4l2loopback-dkms needs them"
        ;;
    fedora)
        $SUDO dnf install -y gcc make rpm-build v4l-utils "kernel-devel-$KREL" 2>/dev/null \
            || $SUDO dnf install -y gcc make rpm-build v4l-utils
        $SUDO dnf install -y v4l2loopback \
            || warn "v4l2loopback not available - enable RPM Fusion (rpmfusion-free) and re-run"
        ;;
    other)
        warn "unknown distro: install gcc, make and v4l2loopback yourself"
        ;;
esac

# ------------------------------------------------------------------- build
step "Compiling and testing"
as_user mkdir -p "$BUILD"
as_user make -C "$SRC" clean all test >/dev/null || fail "build or tests failed (run: make -C $SRC test)"
ok "compiled, all tests passed"
as_user make -C "$SRC" clean >/dev/null
as_user make -C "$SRC" dist DISTDIR="$BUILD" >/dev/null
TARBALL="$BUILD/surface-camd-$VERSION.tar.gz"
ok "source: $TARBALL"

step "Building the $DISTRO package"
PKG=""
case "$DISTRO" in
    arch)
        D="$BUILD/arch"
        as_user mkdir -p "$D"
        as_user cp "$TARBALL" "$D/"
        SUM="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
        as_user sh -c "sed -e 's/@VERSION@/$VERSION/' -e 's/@SHA256@/$SUM/' '$SRC/pkg/PKGBUILD.in' > '$D/PKGBUILD'"
        (cd "$D" && as_user makepkg -f --cleanbuild --noconfirm) || fail "makepkg failed"
        for f in "$D"/surface-camd-"$VERSION"-*.pkg.tar.*; do
            case "$f" in *.sig) ;; *) [ -e "$f" ] && PKG="$f";; esac
        done
        ;;
    debian)
        D="$BUILD/deb"
        STAGE="$D/stage"
        as_user rm -rf "$STAGE"
        as_user mkdir -p "$STAGE/DEBIAN"
        as_user make -C "$SRC" all >/dev/null
        as_user make -C "$SRC" install DESTDIR="$STAGE" PREFIX=/usr \
            SYSTEMDUNITDIR=/usr/lib/systemd/system \
            MODPROBEDIR=/usr/lib/modprobe.d \
            MODULESLOADDIR=/usr/lib/modules-load.d >/dev/null
        as_user make -C "$SRC" clean >/dev/null
        ARCH="$(dpkg --print-architecture)"
        SIZE="$(du -sk "$STAGE" | cut -f1)"
        as_user tee "$STAGE/DEBIAN/control" >/dev/null <<EOF
Package: surface-camd
Version: $VERSION-1
Architecture: $ARCH
Maintainer: linux-surface contrib <surface-camd@localhost>
Installed-Size: $SIZE
Depends: libc6, v4l2loopback-dkms
Section: video
Priority: optional
Homepage: https://github.com/linux-surface/linux-surface
Description: userspace driver for IPU3 Surface cameras (front, rear, IR)
 Captures raw data from the IPU3 CIO2 on Surface Pro 4/5/6 and Surface
 Book 1/2, processes it in software and publishes the cameras as
 v4l2loopback webcams. Does not need libcamera.
EOF
        PKG="$D/surface-camd_${VERSION}-1_${ARCH}.deb"
        as_user dpkg-deb --root-owner-group --build "$STAGE" "$PKG" >/dev/null || fail "dpkg-deb failed"
        ;;
    fedora)
        D="$BUILD/rpm"
        as_user mkdir -p "$D/SOURCES" "$D/SPECS"
        as_user cp "$TARBALL" "$D/SOURCES/"
        as_user sh -c "sed 's/@VERSION@/$VERSION/' '$SRC/pkg/surface-camd.spec.in' > '$D/SPECS/surface-camd.spec'"
        as_user rpmbuild --define "_topdir $D" -bb "$D/SPECS/surface-camd.spec" >/dev/null \
            || fail "rpmbuild failed (run: rpmbuild --define '_topdir $D' -bb $D/SPECS/surface-camd.spec)"
        for f in "$D"/RPMS/*/surface-camd-"$VERSION"-*.rpm; do
            [ -e "$f" ] && PKG="$f"
        done
        ;;
    other)
        as_user make -C "$SRC" all >/dev/null
        ;;
esac
[ "$DISTRO" = other ] || { [ -n "$PKG" ] && [ -e "$PKG" ] || fail "package file not found"; }
[ -n "$PKG" ] && ok "package: $PKG"

if [ "$BUILD_ONLY" = 1 ]; then
    exit 0
fi

# ----------------------------------------------------------------- install
step "Installing"
case "$DISTRO" in
    arch)   $SUDO pacman -U --noconfirm "$PKG";;
    debian) $SUDO apt-get install -y "$PKG";;
    fedora) $SUDO dnf install -y "$PKG";;
    other)  $SUDO make -C "$SRC" install PREFIX=/usr/local; as_user make -C "$SRC" clean >/dev/null;;
esac
ok "installed"

if [ "$START" = 0 ]; then
    echo; echo "Start later with: sudo systemctl enable --now surface-camd@front surface-camd@rear surface-camd@ir"
    exit 0
fi

# ------------------------------------------------------------------- start
step "Loading modules and starting cameras"
$SUDO systemctl daemon-reload
stop_services
# Reload v4l2loopback so it picks up the Surface device names/numbers.
if [ -d /sys/module/v4l2loopback ] && ! $SUDO modprobe -r v4l2loopback 2>/dev/null; then
    warn "v4l2loopback is in use (OBS?); close it and re-run, or reboot"
fi
if ! $SUDO modprobe v4l2loopback; then
    warn "v4l2loopback failed to load. If you just updated the kernel, reboot and re-run."
    warn "Check: dkms status"
fi
$SUDO modprobe ipu3_cio2 2>/dev/null || warn "ipu3_cio2 did not load"

for c in $CAMERAS; do
    $SUDO systemctl enable --now "surface-camd@$c" >/dev/null 2>&1 || true
done
sleep 4

declare -A DEVNODE=([front]=/dev/video50 [rear]=/dev/video51 [ir]=/dev/video52)
FAILED=0
for c in $CAMERAS; do
    if systemctl is-active -q "surface-camd@$c"; then
        ok "$c camera running -> ${DEVNODE[$c]}"
    else
        FAILED=1
        warn "$c camera failed. Last log lines:"
        $SUDO journalctl -u "surface-camd@$c" -n 4 --no-pager -o cat 2>/dev/null | sed 's/^/           /'
    fi
done

echo
if [ "$FAILED" = 0 ]; then
    echo "Done. Test with: ffplay /dev/video50   (or pick \"Surface Front Camera\" in any app)"
    echo "If PipeWire apps don't see them yet: systemctl --user restart wireplumber"
else
    echo "Some cameras did not start. For a full diagnosis run:"
    echo "  sudo $SCRIPT_DIR/surface-camera-setup.sh --report"
    exit 1
fi
