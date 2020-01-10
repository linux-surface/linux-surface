#!/usr/bin/env bash
set -euxo pipefail

# Package compression settings (Matches latest Arch)
export PKGEXT='.pkg.tar.zst'
export COMPRESSZST=(zstd -c -T0 --ultra -20 -)
export MAKEFLAGS="-j2"

# Build the packages
pushd surface-ipts-firmware
makepkg -f --syncdeps --skippgpcheck --noconfirm
popd

pushd kernel
makepkg -f --syncdeps --skippgpcheck --noconfirm
popd
