# Arch Linux PKGBUILDs for Surface Linux

Primarily tested on Arch Linux + Surface Laptop 13" (Intel).

## Instructions

- Building the patched kernel (includes ACPI module)
  ```
  cd kernel
  PKGEXT=".pkg.tar" MAKEFLAGS="-j8" makepkg -s --skippgpcheck
  sudo pacman -U linux-surface-*.pkg.tar
  ```
  It's based on the Arch kernel tree (with patches curated by Arch developers) and Surface specific patches.

- Building firmware:
  Please refer to https://github.com/linux-surface/surface-ipts-firmware

### Advanced users / testers

- Building the ACPI module as a DKMS package (won't work with secure boot):
  Please refer to https://github.com/linux-surface/surface-aggregator-module

