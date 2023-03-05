# Arch Linux PKGBUILDs for Surface Linux

## Instructions

- Building the patched kernel
  ```
  cd kernel
  PKGEXT=".pkg.tar" MAKEFLAGS="-j8" makepkg -s --skippgpcheck
  sudo pacman -U linux-surface-*.pkg.tar
  ```
  It's based on the Arch kernel tree (with patches curated by Arch developers) and Surface specific patches.
