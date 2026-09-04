# Surface Studio 1 modified EDID binaries for the PixelSense Display

When using the proprietary `nvidia` driver the Surface Studio 1's PixelSense Display does not report a proper EDID DisplayID Block with Tiled Display Topology Data to the running system. Consequently, the PixelSense Display will be treated as two separate physical monitors. The problem appears to rest with the `nvidia` driver and not with the Linux kernel.

This tweak provides two modified EDID blobs so Surface Studio 1 systems will recognize the PixelSense Display as one physical monitor on X11 and when using the `nvfbdev` framebuffer device driver. Unfortunately due to issues also appearing to rest with the `nvidia` driver this tweak will not work for Wayland sessions. The EDID blobs were copied from `/sys/class/drm/{card1-eDP-1,card1-eDP-2}/edid` and modified using [Custom Resolution Utility](https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU) to add Tiled Display Topology Data to the blobs.

## Installation

1. Copy `surface-left.bin` and `surface-right.bin` to `/lib/firmware/edid/`
    ```console
    user@example:~$ sudo mkdir -p /lib/firmware/edid
    user@example:~$ sudo cp surface-left.bin surface-right.bin /lib/firmware/edid/
    ```
2. Copy `20-nvidia-edid.conf` to `/etc/X11/xorg.conf.d/`
    ```console
    user@example:~$ sudo cp 20-nvidia-edid.conf /etc/X11/xorg.conf.d/
    ```
3. Add `drm.edid_firmware=eDP-1:edid/surface-left.bin,eDP-2:edid/surface-right.bin` to your kernel boot arguments
    ```console
    user@example:~$ sudo grubby --update-kernel=ALL --args="drm.edid_firmware=eDP-1:edid/surface-left.bin,eDP-2:edid/surface-right.bin"
    ```
4. Restart X11 or reboot your system

## Notes

* While the modified EDID blobs will treat the PixelSense Display as one physical monitor on X11 and when using the `nvfbdev` framebuffer device driver, this tweak will not work for Wayland sessions. This also appears to be an issue with the proprietary `nvidia` driver, not with the Linux kernel.

* Please refer to your distribution's documentation to find the appropriate method to add the `drm.edid_firmware` boot argument to your system. `grubby` is used on Fedora. Other distributions may have different methods.

