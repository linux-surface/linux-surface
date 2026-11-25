# Example thermal-conf.xml File

A thermald configuration file tested on a Surface Laptop 4 (AMD) device. This file is adapted from the surface_laptop_1 configuration file in order to include control over CPU Turbo Boost.

## File Description

A new sensor (CPU_TEMP) is defined as the die temperature of the k10temp sensor, which can be found in the following directory:
```
/sys/class/hwmon/hwmon1/
```
The trip point is set to 65° C, and can be freely adjusted to maintain a desired temperature. The first cooling device that is activated is the Processor cooling device which can be found the thermal directory:
```
/sys/class/thermal/
```

The second cooling device is set to turn off CPU Turbo Boost when the trip point is reached.The status of boost can be queried with the following command:
```
cat /sys/devices/system/cpu/cpufreq/boost
```


## Installation

Deactivate thermald with the following command:

```
sudo systemctl stop thermald.service
```

Place thermal-conf.xml into the following directory:

```
/etc/thermald/
```
Restart thermald:

```
sudo systemctl start thermald.service
```

If it doesn't run, then you may need to remove the --adaptive flag in thermald.service

