#!/usr/bin/env bash
set -euxo pipefail

# Package compression settings (Matches latest Arch)
export PKGEXT='.pkg.tar.zst'
export COMPRESSZST=(zstd -c -T0 --ultra -20 -)

# Import GPG key
echo "$GPG_KEY" | base64 -d | gpg --import --no-tty --batch --yes
export GPG_TTY=$(tty)

# Build the packages as `build' user
pushd surface-ipts-firmware
makepkg -f --syncdeps --skippgpcheck --noconfirm
# Sign as a separate step (makepkg -s needs pinentry)
makepkg --packagelist | xargs -L1 gpg --detach-sign --batch --no-tty --pinentry-mode=loopback --passphrase $GPG_PASSPHRASE -u 5B574D1B513F9A05
popd

pushd kernel
makepkg -f --syncdeps --skippgpcheck --noconfirm
# Sign as a separate step (makepkg -s needs pinentry)
makepkg --packagelist | xargs -L1 gpg --detach-sign --batch --no-tty --pinentry-mode=loopback --passphrase $GPG_PASSPHRASE -u 5B574D1B513F9A05
popd
