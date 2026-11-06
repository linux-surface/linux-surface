# Restore Surface Studio 1 touchscreen functionality after Surface Pen use

Touch inputs stop working once either the pen tip or the eraser tip of a Surface Pen touches the Surface Studio 1's PixelSense Display. Restarting the system or power cycling the Surface Touch Device using `uhubctl` restores functionality.

While this issue may be fixable through patches, these scripts and a `systemd` service provide a workaround in the meantime. It works around the issue by automatically power cycling the Surface Touch Device once a Surface Pen leaves the proximity of the display. A `udev` rule to create a `surface-pen` symlink is additionally provided as the device file may change between power cycles.

## Installation

1. Copy the files to their respective places
    ```console
    user@example:~$ sudo cp 99-surface-pen-symlink.rules /etc/udev/rules.d/
    user@example:~$ sudo cp surface-pen-watcher.service /etc/systemd/system/
    user@example:~$ sudo cp reset-surface-touch.sh /usr/local/bin/
    user@example:~$ sudo cp surface-pen-watcher.sh /usr/local/bin/
    ```
2. Reload `udev` rules
    ```console
    user@example:~$ sudo udevadm control --reload-rules
    user@example:~$ sudo udevadm trigger
    ```
3. Set execute permissions on the scripts
    ```console
    user@example:~$ sudo chmod 755 /usr/local/bin/reset-surface-touch.sh
    user@example:~$ sudo chmod 755 /usr/local/bin/surface-pen-watcher.sh
    ```
4. Reload `systemd` configuration and enable the service
    ```console
    user@example:~$ sudo systemctl daemon-reload
    user@example:~$ sudo systemctl enable --now surface-pen-watcher
    ```

## Notes

These scripts require `uhubctl` and `libinput debug-events`. `libinput debug-events` is available from `libinput-utils` (Fedora) or `libinput-tools` (Debian). These should be available in most distribution package repositories and can be installed using your package manager of choice.

### Hardware assumptions

The `reset-surface-touch.sh` script assumes every Surface Studio 1 system has the same hub vendor ID (`045e:0923`) and port (`3`) used by the Surface Touch Device. Additionally, the `udev` rule assumes every Surface Studio 1 system has the same `ATTRS{name}` (`Surface Touch Device Stylus`), `ATTRS{id/vendor}` (`045e`), and `ATTRS{id/product}` (`0911`). If the scripts don't work, ensure the values are correct:

1. Use `lsusb` to find the hub vendor ID for the `reset-surface-touch.sh` script and the `ATTRS{id/vendor}` and `ATTRS{id/product}` of the Surface Touch Device for the `udev` rule
    ```console
    user@example:~$ lsusb
    ...
    Bus 001 Device 004: ID 045e:0923 Microsoft Corp. USB2.0 Hub
    Bus 001 Device 039: ID 045e:0911 Microsoft Corp. Surface Touch Device
    ...
    ```
2. Use `udevadm info` to find the port the Surface Touch Device is attached to. The format is `/dev/bus/usb/$BUS/$DEVICE`, and the number after the decimal point in the output is the port for the `reset-surface-touch.sh` script
    ```console
    user@example:~$ udevadm info -q path -n /dev/bus/usb/001/039
    /devices/pci0000:00/0000:00:14.0/usb1/1-5/1-5.3
    ```

