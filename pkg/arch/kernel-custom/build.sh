#!/usr/bin/env bash
#
# build.sh - build the custom linux-surface kernel and install it in place of
# linux-surface (Arch-based distros: Arch, EndeavourOS, Nexora, ...).
#
# Usage (as your normal user; sudo is used where needed):
#   ./build.sh                build, install, update bootloader, set up cameras
#   ./build.sh --build-only   only build the packages (into ./work)
#   ./build.sh --prepare-only only prepare ./work for makepkg (used by CI)
#   ./build.sh --install-only install packages already built in ./work
#   ./build.sh --no-camd      don't install the surface-camd camera driver
#   ./build.sh --jobs N       parallel compile jobs (default: all CPUs)
#   ./build.sh --rollback     go back to the linux-surface kernel you had before
#   ./build.sh --clean        delete ./work (the build tree is ~15 GB)
#
# What gets built (see PKGBUILD): the exact linux-surface kernel from
# ../kernel, plus patches-extra/*.patch, custom.config and v4l2loopback.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE="$HERE/../kernel"
WORK="$HERE/work"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
CAMD_INSTALL="$REPO_ROOT/contrib/surface-ipu3-cameras/install.sh"
PREV="$WORK/previous-kernel.txt"
PKGBASE=linux-surface-custom

MODE=all
CAMD=1
JOBS="$(nproc)"
while [ $# -gt 0 ]; do
    case "$1" in
        --build-only) MODE=build; shift;;
        --prepare-only) MODE=prepare; shift;;
        --install-only) MODE=install; shift;;
        --rollback) MODE=rollback; shift;;
        --clean) MODE=clean; shift;;
        --no-camd) CAMD=0; shift;;
        --jobs) JOBS="$2"; shift 2;;
        -h|--help) sed -n '3,18p' "$0"; exit 0;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1;;
    esac
done

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m[ ok ]\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m[warn]\033[0m %s\n' "$*"; }
fail() { printf '  \033[31m[FAIL]\033[0m %s\n' "$*" >&2; exit 1; }

command -v pacman >/dev/null || fail "this builds an Arch Linux package; pacman not found"
[ "$(id -u)" != 0 ] || [ "$MODE" = prepare ] || fail "run as your normal user (makepkg refuses root); sudo is used where needed"

# ------------------------------------------------------------ bootloader
update_bootloader() {
    step "Updating the bootloader"
    if command -v reinstall-kernels >/dev/null 2>&1; then
        # EndeavourOS (systemd-boot / kernel-install)
        sudo reinstall-kernels && ok "reinstall-kernels done"
    elif [ -f /boot/grub/grub.cfg ] && command -v grub-mkconfig >/dev/null 2>&1; then
        sudo grub-mkconfig -o /boot/grub/grub.cfg && ok "GRUB menu regenerated"
    elif command -v bootctl >/dev/null 2>&1 && sudo bootctl is-installed >/dev/null 2>&1; then
        local esp entries made=0
        esp="$(sudo bootctl --print-esp-path 2>/dev/null || echo /boot)"
        entries="$esp/loader/entries"
        # Hand-written systemd-boot entries: make a copy pointing at the new kernel.
        for e in $(sudo sh -c "ls '$entries'/*.conf 2>/dev/null" || true); do
            sudo grep -q 'vmlinuz-linux-surface$' "$e" || continue
            local new="${e%.conf}-custom.conf"
            [ -e "$new" ] && continue
            sudo sh -c "sed -e 's/vmlinuz-linux-surface\$/vmlinuz-$PKGBASE/' \
                -e 's/initramfs-linux-surface\\.img/initramfs-$PKGBASE.img/' \
                -e 's/^title.*/& (custom)/' '$e' > '$new'"
            ok "created $new"
            made=1
        done
        [ "$made" = 1 ] || warn "check $entries: no entry for vmlinuz-$PKGBASE was created; add one if needed"
    else
        warn "unknown bootloader: make sure it boots /boot/vmlinuz-$PKGBASE"
    fi
}

# --------------------------------------------------------------- rollback
if [ "$MODE" = rollback ]; then
    step "Rolling back to linux-surface"
    pkgs=()
    if [ -f "$PREV" ]; then
        while read -r name ver; do
            for f in /var/cache/pacman/pkg/"$name-$ver"-x86_64.pkg.tar.*; do
                case "$f" in *.sig) ;; *) [ -e "$f" ] && pkgs+=("$f");; esac
            done
        done < "$PREV"
    fi
    if [ "${#pkgs[@]}" -gt 0 ]; then
        ok "reinstalling your previous kernel from the pacman cache"
        sudo pacman -U "${pkgs[@]}"
    else
        warn "previous packages not in cache; installing linux-surface from the repository"
        sudo pacman -S linux-surface linux-surface-headers
    fi
    update_bootloader
    echo; echo "Reboot to use linux-surface again."
    exit 0
fi

if [ "$MODE" = clean ]; then
    rm -rf "$WORK"
    ok "removed $WORK"
    exit 0
fi

# ------------------------------------------------------------------ build
find_pkgs() {
    PKGS=()
    for f in "$WORK"/"$PKGBASE"-[0-9]*.pkg.tar.* "$WORK"/"$PKGBASE"-headers-[0-9]*.pkg.tar.*; do
        case "$f" in *.sig) ;; *) [ -e "$f" ] && PKGS+=("$f");; esac
    done
}

if [ "$MODE" = all ] || [ "$MODE" = build ] || [ "$MODE" = prepare ]; then
    step "Preparing $WORK"
    [ -f "$BASE/PKGBUILD" ] || fail "missing $BASE/PKGBUILD"
    mkdir -p "$WORK"
    avail_kb="$(df -Pk "$WORK" | awk 'NR==2{print $4}')"
    if [ "$avail_kb" -lt $((15 * 1024 * 1024)) ]; then
        fail "need ~15 GB free in $WORK, have $((avail_kb / 1024 / 1024)) GB"
    fi

    cp "$BASE/PKGBUILD" "$WORK/PKGBUILD.base"
    cp "$BASE"/*.patch "$BASE"/*.config "$BASE"/config.* "$WORK/"
    cp "$HERE/PKGBUILD" "$HERE/custom.config" "$WORK/"
    rm -f "$WORK"/extra-*.patch
    n=0
    for p in "$HERE"/patches-extra/*.patch; do
        [ -e "$p" ] || continue
        n=$((n + 1))
        cp "$p" "$WORK/extra-$(printf '%02d' "$n")-$(basename "$p")"
    done
    # Optional Secure Boot signing keys (see ../kernel/PKGBUILD)
    for k in MOK.crt MOK.key; do [ -f "$HERE/$k" ] && cp "$HERE/$k" "$WORK/"; done
    ver="$(sed -n 's/^pkgver=//p' "$WORK/PKGBUILD.base")"
    ok "linux-surface $ver + $n extra patch(es) + custom.config + v4l2loopback"

    [ "$MODE" = prepare ] && exit 0

    step "Building (this takes a long time: 1-3 hours on a Surface Pro 5)"
    echo "  log: $WORK/build.log"
    (
        cd "$WORK"
        # PGP signatures of the kernel tarball are skipped like in ../kernel/README.md;
        # the checksums in PKGBUILD.base are still verified.
        MAKEFLAGS="-j$JOBS" makepkg -s -f --cleanbuild --skippgpcheck --noconfirm 2>&1 | tee "$WORK/build.log"
        exit "${PIPESTATUS[0]}"
    ) || fail "build failed; see $WORK/build.log"
    find_pkgs
    [ "${#PKGS[@]}" = 2 ] || fail "expected 2 packages in $WORK, found ${#PKGS[@]}"
    for f in "${PKGS[@]}"; do ok "built $(basename "$f")"; done
fi

[ "$MODE" = build ] && exit 0

# ---------------------------------------------------------------- install
step "Installing (replaces linux-surface and linux-surface-headers)"
find_pkgs
[ "${#PKGS[@]}" = 2 ] || fail "no built packages in $WORK; run ./build.sh --build-only first"

# Remember what we replace, for --rollback.
if pacman -Q linux-surface >/dev/null 2>&1; then
    pacman -Q linux-surface linux-surface-headers 2>/dev/null > "$PREV" || true
    ok "previous kernel recorded: $(tr '\n' ' ' < "$PREV")"
fi
echo "  pacman will ask to remove linux-surface: answer y."
sudo pacman -U "${PKGS[@]}"
ok "installed"

update_bootloader

# ------------------------------------------------------------- cameras
if [ "$CAMD" = 1 ] && [ -x "$CAMD_INSTALL" ]; then
    step "Installing the surface-camd camera driver"
    # The running kernel is still the old one, so only enable the services;
    # they start on the next boot.
    "$CAMD_INSTALL" --no-start
    for c in front rear ir; do sudo systemctl enable "surface-camd@$c" >/dev/null 2>&1 || true; done
    ok "surface-camd enabled for the next boot"
fi

step "Done"
cat <<EOF
  Reboot, choose the "$PKGBASE" entry if your boot menu asks, then check:
    uname -r                  should end in -surface-custom
    $REPO_ROOT/contrib/surface-ipu3-cameras/surface-camera-setup.sh
  To go back:  ./build.sh --rollback
EOF
