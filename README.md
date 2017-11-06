# Linux Surface

Linux running on the Surface Book and Surface Pro 4. Follow the instructions below to install the latest kernel and config files.


### What's Working

* Keyboard (and backlight)
* Touchpad
* 2D/3D Acceleration
* Touchscreen
* Pen (if paired and multi-touch mode enabled)
* WiFi
* Bluetooth
* Speakers
* Power Button
* Volume Buttons
* SD Card Reader
* Cameras (partial support)
* Suspend/Hibernate
* Sensors (accelerometer, gyroscope, ambient light sensor)
* Battery Readings
* Docking/Undocking Tablet and Keyboard (for Surface Book)

### What's NOT Working

* Dedicated GPU (if you have a performance base on a Surface Book, otherwise onboard works fine)
* Cameras (not fully supported yet)

### Download Pre-built Kernel and Headers

Downloads for ubuntu based distros (other distros will need to compile from source in the kernel folder):

https://goo.gl/QSZCwq

You will need to download both the image and headers deb files for the version you want to install.

### Instructions

1. Copy the files under root to where they belong:
  * $ sudo cp -R root/* /
2. Extract ipts_firmware.zip to /lib/firmware/intel/ipts/
  * $ sudo mkdir -p /lib/firmware/intel/ipts
  * $ sudo unzip ipts_firmware.zip -d /lib/firmware/intel/ipts/
3. Extract i915_firmware.zip to /lib/firmware/i915/
  * $ sudo mkdir -p /lib/firmware/i915
  * $ sudo unzip i915_firmware.zip -d /lib/firmware/i915/
4. (Ubuntu 17.10) Fix issue with Suspend to Disk:
  * $ sudo ln -s /lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
5. (all other distros) Fix issue with Suspend to Disk:
  * $ sudo ln -s /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
6. Set permissions on mwifiex_pcie.sh script:
  * $ sudo chown root /lib/systemd/system-sleep/mwifiex_pcie.sh
  * $ sudo chmod 755 /lib/systemd/system-sleep/mwifiex_pcie.sh
7. Install the custom kernel and headers:
  * $ sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb
8. Reboot on installed kernel.

NOTE: If your network won't connect on the 4.14.x series, you need to apply the apparmor-fix-4.14.patch file in /etc/: $ cd /etc/ && sudo patch -p1 < /path/to/apparmor-fix-4.14.patch

### Donations Appreciated!

PayPal: https://www.paypal.me/jakeday42

Bitcoin: 1JkpbAJ41W6SUjH9vCRDpHNNpecjPK3Zid
