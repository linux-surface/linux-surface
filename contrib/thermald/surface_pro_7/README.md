# Using thermald to avoid thermal throttling
1. Install `thermald` for your distro, on many distros it might be installed by default
2. Place thermal-conf.xml into the `/etc/thermald/` directory
3. Depending on your ambient temperature you might want to lower the `<Temperature>` line to make thermald kick in more aggressively. (65000 = 65°C)
4. Place thermal-cpu-cdev-order.xml into the `/etc/thermald/` directory
5. `sudo systemctl restart thermald`

# Making Fedora respect your config files
Fedora uses the `--adaptive` option by default, thus ignoring your config files. This might also apply to some other distros.

Edit `/usr/lib/systemd/system/thermald.service` and remove `--adaptive` from the `ExecStart=` line. Then do a `systemctl daemon-reload` so systemd realizes the change. Thermald should then respect your configuration files.
