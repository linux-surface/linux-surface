# Linux Surface

Linux running on the Surface Book, Surface Book 2, Surface Pro 4, Surface Pro 2017 and Surface Laptop. Follow the instructions below to install the latest kernel and config files.


### What's Working

* Keyboard (and backlight)
* Touchpad
* 2D/3D Acceleration
* Touchscreen
* Pen
* WiFi
* Bluetooth
* Speakers
* Power Button
* Volume Buttons
* SD Card Reader
* Cameras (partial support, disabled for now)
* Hibernate
* Sensors (accelerometer, gyroscope, ambient light sensor)
* Battery Readings
* Docking/Undocking Tablet and Keyboard
* DisplayPort
* Dedicated Nvidia GPU (Surface Book 2)

### What's NOT Working

* Dedicated Nvidia GPU (if you have a performance base on a Surface Book 1, otherwise onboard works fine)
* Cameras (not fully supported yet)
* Suspend (uses Connected Standby which is not supported yet)

### Notes on What's Working
* For the most part, things are tested on a Surface Book. While most things are reportedly fully working on other devices, your mileage may vary. Please look at the issues list for possible exceptions.

### Download Pre-built Kernel and Headers

Downloads for ubuntu based distros (other distros will need to compile from source in the kernel folder):

https://goo.gl/QSZCwq

You will need to download both the image and headers deb files for the version you want to install.

### Instructions

For the ipts_firmware files, please select the version for your device.
* v76 for the Surface Book
* v78 for the Surface Pro 4
* v79 for the Surface Laptop
* v101 for Surface Book 2 15"
* v102 for the Surface Pro 2017
* v137 for the Surface Book 2 13"

1. Copy the files under root to where they belong:
  * $ sudo cp -R root/* /
2. Make /lib/systemd/system-sleep/hibernate.sh as executable:
  * $ sudo chmod a+x /lib/systemd/system-sleep/hibernate.sh
3. Extract ipts_firmware_[VERSION].zip to /lib/firmware/intel/ipts/
  * $ sudo mkdir -p /lib/firmware/intel/ipts
  * $ sudo unzip ipts_firmware_[VERSION].zip -d /lib/firmware/intel/ipts/
4. Extract i915_firmware.zip to /lib/firmware/i915/
  * $ sudo mkdir -p /lib/firmware/i915
  * $ sudo unzip i915_firmware.zip -d /lib/firmware/i915/
5. (Ubuntu 17.10) Fix issue with Suspend to Disk:
  * $ sudo ln -s /lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
6. (all other distros) Fix issue with Suspend to Disk:
  * $ sudo ln -s /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
7. Install the latest marvell firmware:
  * git clone git://git.marvell.com/mwifiex-firmware.git  
  * sudo mkdir -p /lib/firmware/mrvl/  
  * sudo cp mwifiex-firmware/mrvl/* /lib/firmware/mrvl/
8. Install the custom kernel and headers:
  * $ sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb
9. Reboot on installed kernel.

### NOTES

* If you are getting stuck at boot when loading the ramdisk, you need to install the Processor Microcode Firmware for Intel CPUs (usually found under Additional Drivers in Software and Updates).
* If you are having issues with the position of the cursor matching the pen/stylus, you'll need to update your libwacom as mentioned here: https://github.com/jakeday/linux-surface/issues/46

### Donations Appreciated!

PayPal: https://www.paypal.me/jakeday42

Bitcoin: 1AH7ByeJBjMoAwsgi9oeNvVLmZHvGoQg68
