# Arch Linux PKGBUILDs for Surface Linux

Primarily tested on Arch Linux + Surface Laptop 13" (Intel).

## Instructions

- Building firmware & configs
  ```
  cd surface
  makepkg -s
  sudo pacman -U surface-firmware*.pkg.tar.* surface-config*.pkg.tar.*
  ```

- Building the patched kernel (includes ACPI module)
  ```
  cd kernel
  PKGEXT=".pkg.tar" MAKEFLAGS="-j8" makepkg -s --skippgpcheck
  sudo pacman -U linux-surface-*.pkg.tar
  ```
  It's based on the Arch kernel tree (with patches curated by Arch developers) and Surface specific patches.

### Advanced users / testers

- Building the ACPI module as a DKMS package (won't work with secure boot):
  ```
  cd surface-aggregator-module
  makepkg -s
  sudo pacman -U *.pkg.tar.*
  ```
