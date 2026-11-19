#!/usr/bin/env bash
#
# Build the custom Arch packages (runs inside an archlinux container):
#   setup-userspace   install tools for the userspace packages
#   build-userspace   build surface-camd and surface-touch
#   setup-kernel      install linux-surface-custom build dependencies
#   build-kernel      build linux-surface-custom + headers (several hours)
# Packages are collected in pkg/arch/custom-release/.

set -euxo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
RELEASE="$ROOT/pkg/arch/custom-release"
BUILD="$ROOT/pkg/arch/custom-build"

pacman()
{
    command pacman --noconfirm "$@"
}

# Package compression settings (match latest Arch, like arch.sh)
export PKGEXT='.pkg.tar.zst'
export COMPRESSZST=(zstd -c -T0 --ultra -20 -)

# makepkg refuses to run as root
as_nobody()
{
    runuser -u nobody -- "$@"
}

# build_userspace NAME SOURCE_DIR TEMPLATE
build_userspace()
{
    local name="$1" src="$2" template="$3" version sum dir

    version="$(cat "$src/VERSION")"
    dir="$BUILD/$name"
    rm -rf "$dir"
    mkdir -p "$dir"
    make -C "$src" dist DISTDIR="$dir"
    make -C "$src" clean
    sum="$(sha256sum "$dir/$name-$version.tar.gz" | cut -d' ' -f1)"
    sed -e "s/@VERSION@/$version/" -e "s/@SHA256@/$sum/" "$template" > "$dir/PKGBUILD"

    chown -R nobody "$dir"
    (cd "$dir" && as_nobody makepkg -f --noconfirm)
    mkdir -p "$RELEASE"
    find "$dir" -maxdepth 1 -name '*.pkg.tar.zst' -exec mv {} "$RELEASE/" \;
}

case "${1:-}" in
setup-userspace)
    pacman -Syu
    pacman -S --needed base-devel git python
    ;;
build-userspace)
    build_userspace surface-camd \
        "$ROOT/contrib/surface-ipu3-cameras/surface-camd" \
        "$ROOT/contrib/surface-ipu3-cameras/surface-camd/pkg/PKGBUILD.in"
    build_userspace surface-touch \
        "$ROOT/contrib/surface-touch" \
        "$ROOT/contrib/surface-touch/pkg/PKGBUILD.in"
    ls -la "$RELEASE"
    ;;
setup-kernel)
    pacman -Syu
    pacman -S --needed sudo binutils fakeroot base-devel git

    # Same makedepends as the official linux-surface PKGBUILD, plus git
    # (v4l2loopback is a git source).
    deps=$(bash -c 'cd "$1" && source PKGBUILD && echo "${makedepends[@]}"' _ "$ROOT/pkg/arch/kernel")
    # shellcheck disable=SC2086
    pacman -S --needed ${deps} git
    ;;
build-kernel)
    cd "$ROOT/pkg/arch/kernel-custom"
    ./build.sh --prepare-only

    chown -R nobody .
    # PGP signatures are skipped like in arch.sh; checksums are still verified.
    (cd work && as_nobody env MAKEFLAGS="-j$(nproc)" makepkg -f --cleanbuild --skippgpcheck --noconfirm)

    mkdir -p "$RELEASE"
    find work -maxdepth 1 -name 'linux-surface-custom*.pkg.tar.zst' -exec mv {} "$RELEASE/" \;
    ls -la "$RELEASE"
    ;;
*)
    sed -n '3,9p' "$0"
    exit 1
    ;;
esac
