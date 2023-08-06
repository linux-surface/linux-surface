# Using thermald to avoid thermal throttling
Install `thermald` for your distro, on many distros it might be installed by default.

Create the config file `/etc/thermald/thermal-conf.xml` and add the following content to the file:

```xml
<?xml version="1.0"?>
<ThermalConfiguration>
<Platform>
	<Name>Surface Pro 7 Thermal Workaround</Name>
	<ProductName>*</ProductName>
	<Preference>QUIET</Preference>
	<ThermalZones>
		<ThermalZone>
			<Type>cpu</Type>
			<TripPoints>
				<TripPoint>
					<SensorType>x86_pkg_temp</SensorType>
					<Temperature>65000</Temperature>
					<type>passive</type>
					<ControlType>SEQUENTIAL</ControlType>
					<CoolingDevice>
						<index>1</index>
						<type>rapl_controller</type>
						<influence>100</influence>
						<SamplingPeriod>10</SamplingPeriod>
					</CoolingDevice>
				</TripPoint>
			</TripPoints>
		</ThermalZone>
	</ThermalZones>
</Platform>
</ThermalConfiguration>
```
Depending on your ambient temperature you might want to lower the `<Temperature>` line to make thermald kick in more aggressively. (65000 = 65°C)

Create the file `/etc/thermald/thermal-cpu-cdev-order.xml` with the following content:
```xml
<CoolingDeviceOrder>
	<CoolingDevice>rapl_controller</CoolingDevice>
	<CoolingDevice>intel_pstate</CoolingDevice>
	<CoolingDevice>intel_powerclamp</CoolingDevice>
	<CoolingDevice>cpufreq</CoolingDevice>
	<CoolingDevice>Processor</CoolingDevice>
</CoolingDeviceOrder>
```

# Making Fedora respect your config files
Fedora uses the `--adaptive` option by default, thus ignoring your config files. This might also apply to some other distros.

Edit `/usr/lib/systemd/system/thermald.service` and remove `--adaptive` from the `ExecStart=` line. Then do a `systemctl daemon-reload` so systemd realizes the change. thermald should then respect your configuration files.
