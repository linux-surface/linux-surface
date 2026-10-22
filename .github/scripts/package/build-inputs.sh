#!/usr/bin/env bash
#
# Print the git tree IDs of everything a package build depends on.
#
# Builds of pull requests record this, so that packages built for a pull
# request can be released after merging without rebuilding them, as long as
# the inputs did not change in between.
#
# Usage: build-inputs.sh <distro> [<commit>]

set -euo pipefail

DISTRO="${1:-}"
COMMIT="${2:-HEAD}"

case "${DISTRO}" in
arch)
    SERIES="$(git show "${COMMIT}:pkg/arch/kernel/PKGBUILD" \
        | sed -nE 's/^pkgver=([0-9]+\.[0-9]+)\..*$/\1/p')"

    PATHS=(
        "patches/${SERIES}"
        "configs/surface-${SERIES}.config"
        pkg/keys
        pkg/arch/kernel
        .github/scripts/package/arch.sh
        .github/scripts/container
    )
    ;;
fedora)
    SERIES="$(git show "${COMMIT}:pkg/fedora/kernel-surface/build-linux-surface.py" \
        | sed -nE 's/^PACKAGE_TAG = "kernel-([0-9]+\.[0-9]+)\..*"$/\1/p')"

    PATHS=(
        "patches/${SERIES}"
        "configs/surface-${SERIES}.config"
        pkg/keys
        pkg/fedora/kernel-surface
        .github/scripts/package/fedora.sh
        .github/scripts/container
    )
    ;;
debian)
    SERIES="$(git show "${COMMIT}:pkg/debian/kernel/version.conf" \
        | sed -nE 's/^KERNEL_VERSION="([0-9]+\.[0-9]+)\..*"$/\1/p')"

    PATHS=(
        "patches/${SERIES}"
        "configs/surface-${SERIES}.config"
        pkg/keys
        pkg/debian
        .github/scripts/package/debian.sh
        .github/scripts/container
    )
    ;;
*)
    echo "Usage: $0 {arch|debian|fedora} [<commit>]" >&2
    exit 1
    ;;
esac

for path in "${PATHS[@]}"; do
    echo "${path} $(git rev-parse --verify "${COMMIT}:${path}")"
done
