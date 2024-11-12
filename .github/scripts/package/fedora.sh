#!/usr/bin/env bash

set -euxo pipefail

if [ -z "$1" ]; then
    $0 setup-builddeps
    $0 setup-secureboot
    $0 build-packages
    $0 sign-packages
    exit
fi

dnf()
{
    command dnf -y "$@"
}

case "$1" in
setup-builddeps)
    # Setup build environment
    dnf distro-sync
    dnf install @rpm-development-tools git rpm-sign

    # Install build dependencies
    dnf builddep kernel

    # Install additional build dependencies
    dnf install sbsigntools
    ;;
setup-secureboot)
    if [ -z "${SB_KEY:-}" ]; then
        echo "WARNING: No secureboot key configured, skipping signing."
        exit
    fi

    # Install the surface secureboot certificate
    echo "${SB_KEY}" | base64 -d > pkg/fedora/kernel-surface/secureboot/MOK.key
    cp pkg/keys/surface.crt pkg/fedora/kernel-surface/secureboot/MOK.crt
    ;;
build-packages)
    pushd pkg/fedora/kernel-surface || exit 1

    # setup git
    git config --global user.name "surfacebot"
    git config --global user.email "surfacebot@users.noreply.github.com"

    # Build source RPM packages
    python3 build-linux-surface.py --mode srpm --ark-dir kernel-ark --outdir srpm

    # Remove the kernel-ark tree to get as much free disk space as possible
    rm -rf kernel-ark

    # Build binary RPM packages
    find srpm -name '*.src.rpm' -type f -print0 | xargs -0 -I '{}' \
        rpmbuild -rb --define "_topdir ${PWD}/rpmbuild" --define "_rpmdir ${PWD}/out" {}

    popd || exit 1
    ;;
sign-packages)
    if [ -z "${GPG_KEY:-}" ] || [ -z "${GPG_KEY_ID:-}" ]; then
        echo "WARNING: No GPG key configured, skipping signing."
        exit
    fi

    pushd pkg/fedora/kernel-surface/out/x86_64 || exit 1

    # import GPG key
    echo "${GPG_KEY}" | base64 -d | gpg --import --no-tty --batch --yes

    # sign packages
    find . -name '*.rpm' -type f -print0 | xargs -0 -I '{}' \
        rpm --resign {} --define "_gpg_name ${GPG_KEY_ID}"

    popd || exit 1
    ;;
esac
