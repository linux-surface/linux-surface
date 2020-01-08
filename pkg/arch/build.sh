#!/usr/bin/env bash
set -euxo pipefail

# Package compression settings (Matches latest Arch)
export PKGEXT='.pkg.tar.zst'
export COMPRESSZST=(zstd -c -T0 --ultra -20 -)

# Create user
useradd -m -g wheel -s /bin/sh tester
echo "nobody ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
chown -R nobody:wheel .

# Install makepkg deps
pacman -Sy sudo binutils fakeroot grep base-devel git --noconfirm

# Build the packages as `nobody' user
# TODO: use --sign --key <key>
pushd pkg/arch/surface
su nobody -p -s /bin/bash -c 'makepkg -f --syncdeps --skippgpcheck --noconfirm'
popd

pushd pkg/arch/kernel
# su nobody -p -s /bin/bash -c 'makepkg -f --syncdeps --skippgpcheck --noconfirm'
popd
