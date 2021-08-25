# thermald configuration tweaked for Surface Pro 5

- thermald configuration starting point: https://github.com/intel/dptfxtract
    - by default it uses sensor "GEN4", which is for the NVME drive, not good.
    - pch_skylake is a better choice here.
- Takes care of `surface profile set {low-power|performance}` on power supply events.
- Restarts thermald on power events to load corresponding thermal profiles.
- There are 3 thermal profiles here:
    - `thermal-conf.xml.auto.performance`: activated on AC power. 
      Runs at peak performance (~25watts) until reaching 60 degrees.
      Stabilized at ~15watts under heavy load. 
    - `thermal-conf.xml.auto.mobile`: activated on battery.
      Throttles early for ~15watts.
      Throttles to ~8watts for heavy load.
      Try to keep the device cool.
    - `thermal-conf.xml.auto.cool`: reserved.

Basically, we are just using RAPL here to throttle the power (in watts), not
the frequency of the CPU, and it's more fine-grained and aligned with our
thermal targets.

See 40-surface-power.rules for more details on how to obtain info about sensors
and cooling devices.

## Installation

- Make sure to install `surface-control`
- Install `40-surface-power.rules` to `/usr/lib/udev/rules.d/`
- Install `thermal-conf.xml*` to `/etc/thermald/`
    - `thermal-conf.xml.auto` will be linked dynamically by the udev rule file.
- Install `thermal-cpu-cdev-order.xml` to `/etc/thermald/`
- Install `thermald.service` to `/lib/systemd/system/` and overwrite the old one.
    - Maybe install to `/usr/lib/systemd/system/` too.
- `systemctl daemon-reload`
- `systemctl restart udev`
- `systemctl restart thermald`

## TODO

- I tried to update cpufreq governor and turbo in udev event triggers but it
  didn't work.
- You can install `cpufreqctl` for that.
