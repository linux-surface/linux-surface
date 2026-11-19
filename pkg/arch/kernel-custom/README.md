# Custom linux-surface kernel (Arch-based distros)

This builds `linux-surface-custom` and `linux-surface-custom-headers`. They
install **in place of** `linux-surface` and `linux-surface-headers`. Use it
on Arch, EndeavourOS, Nexora or any other pacman-based distro.

## What's in it

The PKGBUILD doesn't copy the official recipe. It loads `../kernel/PKGBUILD`
and extends it. So it always builds the same kernel version, Arch patch,
Surface patches and config as the official `linux-surface` package. When
this repo updates `../kernel`, your next build picks the update up
automatically. On top of that, it adds:

| Addition | Where | Why |
|---|---|---|
| Your own kernel patches | `patches-extra/*.patch` | e.g. a fix for the Surface Pro 5 cameras once `surface-camera-setup.sh --report` shows the failure |
| Your config changes | `custom.config` | merged last; keeps the IPU3 camera drivers and S0ix counters on |
| v4l2loopback 0.15.4 | built with the kernel | `surface-camd` works with no DKMS and no headers |

## How it replaces linux-surface

- The kernel package `provides=linux-surface` and `conflicts=linux-surface`.
  The headers package does the same for `linux-surface-headers`.
  - pacman removes the old packages in the same transaction that installs
    the new ones.
  - Anything that depends on `linux-surface` stays satisfied.
- The package names are different, so `pacman -Syu` from the linux-surface
  repo won't silently swap it back.
- The kernel release is `…-surface-custom`, with its own module directory
  and boot files. It never mixes files with a stock kernel.

## Use

```sh
cd pkg/arch/kernel-custom
./build.sh                # build, install, update bootloader, enable cameras
reboot
uname -r                  # -> 7.3.4-arch1-1-surface-custom
```

`build.sh` runs as your normal user and asks for sudo when needed. It:

1. Prepares `work/`: the official PKGBUILD and patches, plus your extras.
2. Runs `makepkg` with all CPU cores, logging to `work/build.log`.
   - **This takes 1–3 hours on a Surface Pro 5** and needs about 15 GB free.
   - Plug in the charger.
3. Records which `linux-surface` you had, then installs the new packages.
   pacman asks to remove `linux-surface`: answer `y`.
4. Updates the bootloader:
   - EndeavourOS / kernel-install: `reinstall-kernels`
   - GRUB: `grub-mkconfig`
   - Hand-written systemd-boot entries: creates a copy pointing at
     `vmlinuz-linux-surface-custom`
5. Installs the `surface-camd` camera driver and enables it for the next
   boot.

| Option | Effect |
|---|---|
| `--build-only` | Only build (e.g. on a faster PC; copy `work/*.pkg.tar.zst` over and run `--install-only`) |
| `--install-only` | Install packages already in `work/` |
| `--no-camd` | Skip the camera driver |
| `--jobs N` | Compile jobs (default: all CPUs) |
| `--rollback` | Put back the `linux-surface` you had before (from the pacman cache, or the repo) |
| `--clean` | Delete `work/` |

## Build on GitHub instead

The `Arch (custom packages)` workflow (`.github/workflows/arch-custom.yml`)
builds the kernel and headers, plus `surface-camd` and `surface-touch`, on
GitHub's servers. Run it from the repository's **Actions** tab ("Run
workflow"). It also runs on pushes that change these packages.

Download the `arch-custom-kernel` and `arch-custom-userspace` artifacts,
unzip them, and install:

```sh
sudo pacman -U linux-surface-custom-*.pkg.tar.zst   # answer y to replace linux-surface
./build.sh --install-only    # alternative: put the files in work/ and let build.sh
                             # also update the bootloader and set up the cameras
```

## Secure Boot

Put `MOK.crt` and `MOK.key` in this directory and install `sbsigntools`.
The kernel image is then signed during the build, the same way as in
`../kernel`.
