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
You can do so by running `sudo systemctl edit thermald.service`.

Note that with `systemctl edit`, before the modified `ExecStart` line, you should add a new line containing `ExecStart=` and nothing on the right, like this:

```
ExecStart=
ExecStart=<modified line>
```

This instructs to replace the existing `ExecStart` instead of adding a new one. Otherwise, you may get an error "Unit thermald.service has a bad unit file setting", and the command `systemctl status thermald.service` may prompt a message "Service has more than one ExecStart= setting".
