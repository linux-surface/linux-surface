# Example Thermald Configuration

This is a minimal thermald configuration, which sets the maximum sustained CPU temperature to about 65°C.
Modify the `<Temperature>65000</Temperature>` value to adapt that to your liking.

Tested on a Surface Book 2, other devices may need adapting, see e.g. the [thermald man page](http://manpages.ubuntu.com/manpages/trusty/man5/thermal-conf.xml.5.html).

More complex and device specific examples can be found in subdirectories
If any subdirectory does not have both files or any dedicated instructions, use the missing files provided here and/or follow these instructions.

## Installation

Both XML files (`thermal-conf.xml` and `thermal-cpu-cdev-order-xml`) need to be placed in the `/etc/thermald/` directory.

Newer thermald versions attempt to automatically load the configuration from ACPI.
If you want to use a manual configuration with such a version, you may need to remove the `--adaptive` option from the systemd service `ExecStart` line.
You can do so by overwriting `thermald.service`. This file is present in `/lib/systemd/system/` (it may also be in `/usr/lib/systemd/system/`).
