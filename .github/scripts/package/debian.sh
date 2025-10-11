#!/usr/bin/env bash

set -euxo pipefail

if [ -z "${1:-}" ]; then
    $0 setup-builddeps
    $0 setup-secureboot
    $0 build-packages
    $0 sign-packages
    exit
fi

apt-get()
{
    command apt-get -y "$@"
}

MAINLINE_REPO="git://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack"
MAINLINE_BRANCH="cod/mainline"

case "${1:-}" in
setup-builddeps)
    export PATH="$HOME/.cargo/bin:$PATH"

    SOURCES="$(sed 's/^deb /deb-src /' /etc/apt/sources.list)"
    echo "${SOURCES}" >> /etc/apt/sources.list

    ln -snf /usr/share/zoneinfo/UTC /etc/localtime
    echo UTC > /etc/timezone
    
    apt-get update
    apt-get upgrade
    apt-get install build-essential fakeroot rsync git wget software-properties-common \
            zstd lz4 sbsigntool debhelper dpkg-dev dpkg-sig pkg-config \
            llvm-15 clang-15 libclang-15-dev 
    apt-get build-dep linux

    # install python 3.11, required for configuring the kernel via Ubuntu's annotation format
    add-apt-repository -y ppa:deadsnakes

    apt-get update
    apt-get upgrade
    apt-get install python3.11

    rm -f /usr/bin/python
    rm -f /usr/bin/python3
    ln -s /usr/bin/python3.11 /usr/bin/python
    ln -s /usr/bin/python3.11 /usr/bin/python3

    # install rust dependencies (required for now because of minimum version requirements)
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    rustup component add rust-src rustfmt clippy
    cargo install --locked bindgen-cli
    ;;
setup-secureboot)
    if [ -z "${SB_KEY:-}" ]; then
        echo "WARNING: No secureboot key configured, skipping signing."
        exit
    fi

    mkdir -p pkg/debian/kernel/keys

    # Install the surface secureboot certificate
    echo "${SB_KEY}" | base64 -d > pkg/debian/kernel/keys/MOK.key
    cp pkg/keys/surface.crt pkg/debian/kernel/keys/MOK.crt
    ;;
build-packages)
    export PATH="$HOME/.cargo/bin:$PATH"

    pushd pkg/debian/kernel || exit 1

    . version.conf

    # setup git
    git config --global user.name "surfacebot"
    git config --global user.email "surfacebot@users.noreply.github.com"

    # get ubuntu mainline source
    # see https://kernel.ubuntu.com/~kernel-ppa/mainline
    git clone "${MAINLINE_REPO}" --branch "${MAINLINE_BRANCH}/v${KERNEL_VERSION}" --depth 1 linux

    if [ -d "keys" ]; then
        mv keys linux
    fi

    pushd linux || exit 1

    # apply surface build/packaging patches
    find .. -name '*.patch' -type f -print0 | sort -z | xargs -0 -I '{}' \
        git apply --index --reject {}

    git add .
    git commit --allow-empty -m "Apply linux-surface packaging patches"

    KERNEL_MAJORVER="${KERNEL_VERSION%.*}"
    PATCHES="../../../../patches/${KERNEL_MAJORVER}"

    # apply surface patches
    find "${PATCHES}" -name '*.patch' -type f -print0 | sort -z | xargs -0 -I '{}' \
        git apply --index --reject {}

    git add .
    git commit --allow-empty -m "Apply linux-surface patches"

    # generate base config
    ./debian/scripts/misc/annotations --arch amd64 --flavour generic --export > ../base.config

    # merge configs
    ./scripts/kconfig/merge_config.sh \
        ../base.config \
        ../ubuntu.config \
        "../../../../configs/surface-${KERNEL_MAJORVER}.config"
    
    # Explicitly set package version, including revision. This is picked up by 'make bindeb-pkg'.
    export KDEB_PKGVERSION="${KERNEL_VERSION}${KERNEL_LOCALVERSION}-${KERNEL_REVISION}"
    
    # The DPKG in Ubuntu 22.04 defaults to using ZSTD, which is not yet supported by the DPKG in Debian 11
    export KDEB_COMPRESS="xz"

    # Set kernel localversion
    export LOCALVERSION="${KERNEL_LOCALVERSION}-${KERNEL_REVISION}"

    # Get debug info for rust
    make rustavailable

    make bindeb-pkg -j "$(nproc)"

    popd || exit 1
    popd || exit 1

    pushd pkg/debian/meta || exit 1

    ./mkdebian.sh "$(make -C ../kernel/linux -s kernelrelease)"
    dpkg-buildpackage -b -Zxz

    popd || exit 1

    pushd pkg/debian || exit 1

    mkdir release

    find . -name 'linux-libc-dev*.deb' -type f -print0 | xargs -0 -I '{}' rm {}
    find . -name '*.deb' -type f -print0 | xargs -0 -I '{}' cp {} release

    popd || exit 1
    ;;
sign-packages)
    if [ -z "${GPG_KEY:-}" ] || [ -z "${GPG_KEY_ID:-}" ]; then
        echo "WARNING: No GPG key configured, skipping signing."
        exit
    fi

    pushd pkg/debian/release || exit 1

    # import GPG key
    echo "${GPG_KEY}" | base64 -d | gpg --import --no-tty --batch --yes

    # sign packages
    find . -name '*.deb' -type f -print0 | xargs -0 -I '{}' \
        dpkg-sig -g "--batch --no-tty" --sign builder -k "${GPG_KEY_ID}" {}

    popd || exit 1
    ;;
esac
