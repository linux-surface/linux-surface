#!/usr/bin/env bash

set -euxo pipefail

tag()
{
    DISTRO="${1:-}"
    VERSION="${2:-}"

    if [ -z "${DISTRO}" ]; then
        echo "DISTRO missing!"
        return 1
    fi

    if [ -z "${VERSION}" ]; then
        echo "VERSION missing!"
        return 1
    fi

    git tag -f "${DISTRO}-${VERSION}"
    git push --force origin "${DISTRO}-${VERSION}"
}

getarch()
{
    VERSION="$(grep 'pkgver=' pkg/arch/kernel/PKGBUILD | cut -d'=' -f2 | cut -d'.' -f1-3)"
    RELEASE="$(grep 'pkgrel=' pkg/arch/kernel/PKGBUILD | cut -d'=' -f2)"

    if [ -z "${VERSION}" ]; then
        echo "VERSION missing!"
        return 1
    fi

    if [ -z "${RELEASE}" ]; then
        echo "RELEASE missing!"
        return 1
    fi

    echo "${VERSION}-${RELEASE}"
}

getdebian()
{
    . pkg/debian/kernel/version.conf

    if [ -z "${KERNEL_VERSION}" ]; then
        echo "KERNEL_VERSION missing!"
        return 1
    fi

    if [ -z "${KERNEL_REVISION}" ]; then
        echo "KERNEL_REVISION missing!"
        return 1
    fi

    echo "${KERNEL_VERSION}-${KERNEL_REVISION}"
}

getfedoravar()
{
    VAR="${1:-}"

    if [ -z "${VAR}" ]; then
        echo "VAR is missing!"
        return 1
    fi

    grep -E "${VAR} = \"([^\"]+)\"" pkg/fedora/kernel-surface/build-linux-surface.py | sed "s|^${VAR} = \"||g" | sed 's|"$||g'
}

getfedora()
{
    VERSION="$(getfedoravar PACKAGE_TAG | cut -d'-' -f2)"
    RELEASE="$(getfedoravar PACKAGE_RELEASE)"

    if [ -z "${VERSION}" ]; then
        echo "VERSION missing!"
        return 1
    fi

    if [ -z "${RELEASE}" ]; then
        echo "RELEASE missing!"
        return 1
    fi

    echo "${VERSION}-${RELEASE}"
}

tag 'arch' "$(getarch)"
tag 'debian' "$(getdebian)"
tag 'fedora-40' "$(getfedora)"
tag 'fedora-41' "$(getfedora)"
