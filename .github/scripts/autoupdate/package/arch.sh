#!/usr/bin/env bash
#
# Arch Linux package update helper
#
# Usage:
#   arch.sh current          Print the currently packaged version
#   arch.sh latest           Print the latest upstream versions for the series
#   arch.sh check <version>  Check whether upstream sources for <version> exist
#
# Upstream sources:
#   - kernel.org release tarball
#   - Arch kernel patch (release asset on github.com/archlinux/linux)
#   - Arch packaging (gitlab.archlinux.org), providing config.x86_64

# shellcheck source-path=SCRIPTDIR source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

PKGBUILD="${REPO_ROOT}/pkg/arch/kernel/PKGBUILD"

ARCH_KERNEL_REPO="archlinux/linux"
ARCH_PACKAGING_REPO="https://gitlab.archlinux.org/archlinux/packaging/packages/linux.git"

pkgbuild_var()
{
    sed -nE "s/^$1=(.*)\$/\\1/p" "${PKGBUILD}" | head -n1
}

current_version()
{
    pkgbuild_var pkgver | sed -E 's/\.arch[0-9]+$//'
}

cmd_current()
{
    local version

    version="$(current_version)"

    emit series "$(version_series "${version}")"
    emit version "${version}"
    emit release "$(pkgbuild_var pkgrel)"
    emit package_version "$(pkgbuild_var pkgver)-$(pkgbuild_var pkgrel)"
}

cmd_latest()
{
    local series
    local tag

    series="$(version_series "$(current_version)")"

    tag="$(github_tags "${ARCH_KERNEL_REPO}" "v${series}." "v$(regex_escape "${series}")\.[0-9]+-arch[0-9]+" | tail -n1)"

    emit upstream "$(echo "${tag#v}" | tr - .)"
}

cmd_check()
{
    local version
    local major
    local tarball
    local tag
    local asset
    local pkgver
    local pkgtag

    version="$(parse_version "${1:-}")"
    major="${version%%.*}"

    # Kernel tarball
    tarball="https://cdn.kernel.org/pub/linux/kernel/v${major}.x/linux-${version}.tar.xz"

    if ! url_exists "${tarball}"; then
        emit status pending
        emit reason "kernel.org tarball for ${version} not available"
        return "${EXIT_PENDING}"
    fi

    # Arch kernel patch (prefer the latest archN release)
    tag="$(github_tags "${ARCH_KERNEL_REPO}" "v${version}-arch" "v$(regex_escape "${version}")-arch[0-9]+" | tail -n1)"

    if [ -z "${tag}" ]; then
        emit status pending
        emit reason "archlinux/linux has no v${version}-archN tag yet"
        return "${EXIT_PENDING}"
    fi

    asset="https://github.com/${ARCH_KERNEL_REPO}/releases/download/${tag}/linux-${tag}.patch.zst"

    if ! url_exists "${asset}"; then
        emit status pending
        emit reason "archlinux/linux release ${tag} has no patch asset yet"
        return "${EXIT_PENDING}"
    fi

    # Arch packaging (for config.x86_64), e.g. v7.2.11-arch1 -> 7.2.11.arch1-N
    pkgver="$(echo "${tag#v}" | tr - .)"
    pkgtag="$(remote_tags "${ARCH_PACKAGING_REPO}" "${pkgver}-" "$(regex_escape "${pkgver}")-[0-9]+" | tail -n1)"

    if [ -z "${pkgtag}" ]; then
        emit status pending
        emit reason "Arch packaging has no ${pkgver}-N tag yet"
        return "${EXIT_PENDING}"
    fi

    emit status ready
    emit upstream "${pkgver}"
    emit arch_tag "${tag}"
    emit packaging_tag "${pkgtag}"
}

case "${1:-}" in
current)
    cmd_current
    ;;
latest)
    cmd_latest
    ;;
check)
    cmd_check "${2:-}"
    ;;
*)
    die "Usage: $0 {current|latest|check <version>}"
    ;;
esac
