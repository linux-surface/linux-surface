# Recognize the Surface Studio 1's PixelSense Display as one physical monitor

When using the proprietary `nvidia` driver the Surface Studio 1's PixelSense Display does not report a proper EDID DisplayID Block with Tiled Display Topology Data to the running system. Consequently, the PixelSense Display will be treated as two separate physical monitors. The problem appears to rest with the `nvidia` driver and not with the Linux kernel.

This tweak provides two modified EDID blobs so Surface Studio 1 systems will recognize the PixelSense Display as one physical monitor on X11, on certain Wayland sessions, and when using the `nvfbdev` framebuffer device driver. The EDID blobs were copied from `/sys/class/drm/{card1-eDP-1,card1-eDP-2}/edid` and modified using [Custom Resolution Utility](https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU) to add Tiled Display Topology Data to the blobs.

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
4. Copy `modeset.conf` to `/etc/nvidia/`
    ```console
    user@example:~$ sudo mkdir -p /etc/nvidia
    user@example:~$ sudo cp modeset.conf /etc/nvidia/
    ```
5. Copy `nvidia-modeset.conf` to `/etc/modprobe.d/`
    ```console
    user@example:~$ sudo cp nvidia-modeset.conf /etc/modprobe.d/
    ```
6. Disable all graphical boot splash screens like Plymouth on Fedora
    ```console
    user@example:~$ sudo grubby --update-kernel=ALL --remove-args="rhgb"
    ```
7. Reboot your system

## Notes

* As of Fedora 45, Gnome (Wayland), Cinnamon (Wayland, Experimental), and X11 sessions recognize the PixelSense Display as one physical monitor. Sway continues to treat each half of the PixelSense Display as separate outputs. I have not tested any other kinds of Wayland sessions other than Gnome, Cinnamon, and Sway.

* Please refer to your distribution's documentation to find the appropriate method to add the `drm.edid_firmware` boot argument to your system and to disable all graphical boot splash screens like Plymouth. `grubby` is used on Fedora. Other distributions may have different methods.

