# Linux Surface

Linux running on the Surface Book, Surface Book 2, Surface Pro 3, Surface Pro 4, Surface Pro 2017 and Surface Laptop. Follow the instructions below to install the latest kernel and config files.


### What's Working

* Keyboard (and backlight)
* Touchpad
* 2D/3D Acceleration
* Touchscreen
* Pen
* WiFi
* Bluetooth
* Speakers
* Power Button (not yet working for SB2/SP2017)
* Volume Buttons (not yet working for SB2/SP2017)
* SD Card Reader
* Cameras (partial support, disabled for now)
* Hibernate
* Sensors (accelerometer, gyroscope, ambient light sensor)
* Battery Readings (not yet working for SB2/SP2017)
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

https://github.com/jakeday/linux-surface/releases

You will need to download both the image and headers deb files for the version you want to install.

### Instructions

For the ipts_firmware files, please select the version for your device.
* v76 for the Surface Book
* v78 for the Surface Pro 4
* v79 for the Surface Laptop
* v101 for Surface Book 2 15"
* v102 for the Surface Pro 2017
* v137 for the Surface Book 2 13"

For the i915_firmware files, please select the version for your device.
* kbl for series 5 devices (Surface Book 2, Surface Pro 2017)
* skl for series 4 devices (Surface Book, Surface Pro 4, Surface Laptop)
* bxt for series 3 devices (Surface Pro 3)

1. Copy the files under root to where they belong:
  * $ sudo cp -R root/* /
2. Make /lib/systemd/system-sleep/hibernate.sh as executable:
  * $ sudo chmod a+x /lib/systemd/system-sleep/hibernate.sh
3. Extract ipts_firmware_[VERSION].zip to /lib/firmware/intel/ipts/
  * $ sudo mkdir -p /lib/firmware/intel/ipts
  * $ sudo unzip firmware/ipts_firmware_[VERSION].zip -d /lib/firmware/intel/ipts/
4. Extract i915_firmware_[VERSION].zip to /lib/firmware/i915/
  * $ sudo mkdir -p /lib/firmware/i915
  * $ sudo unzip firmware/i915_firmware_[VERSION].zip -d /lib/firmware/i915/
5. (Ubuntu 17.10) Fix issue with Suspend to Disk:
  * $ sudo ln -s /lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
6. (all other distros) Fix issue with Suspend to Disk:
  * $ sudo ln -s /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
7. Install the latest marvell firmware (if their repo is down, use my copy at https://github.com/jakeday/mwifiex-firmware):
  * git clone git://git.marvell.com/mwifiex-firmware.git  
  * sudo mkdir -p /lib/firmware/mrvl/  
  * sudo cp mwifiex-firmware/mrvl/* /lib/firmware/mrvl/
8. Install the custom kernel and headers (or follow the steps for compiling the kernel from source below):
  * $ sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb
9. Reboot on installed kernel.

### NOTES

* If you are getting stuck at boot when loading the ramdisk, you need to install the Processor Microcode Firmware for Intel CPUs (usually found under Additional Drivers in Software and Updates).
* If you are having issues with the position of the cursor matching the pen/stylus, you'll need to update your libwacom as mentioned here: https://github.com/jakeday/linux-surface/issues/46

### Compiling the Kernel from Source

If you don't want to use the pre-built kernel and headers, you can compile the kernel yourself following these steps:


0. (Prep) Install the required packages for compiling the kernel:
  * $ sudo apt-get install build-essential binutils-dev libncurses5-dev libssl-dev ccache bison flex
1. Assuming you cloned the linux-surface repo (this one) into ~/linux-surface, go to the parent directory:
  * $ cd ~
2. Clone the mainline stable kernel repo:
  * $ git clone git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
3. Go into the linux-stable directory:
  * $ cd linux-stable
4. Checkout the version of the kernel you wish to target (replacing with your target version):
  * $ git checkout v4.y.z
5. Apply the kernel patches from the linux-surface repo (this one):
  * $ for i in ~/linux-surface/patches-[VERSION]/*.patch; do patch -p1 < $i; done
6. Copy over the config file from the linux-surface repo (this one):
  * $ cp ~/linux-surface/config .config
7. Compile the kernel and headers (for ubuntu, refer to the build guild for your distro):
  * $ make -j \`getconf _NPROCESSORS_ONLN\` deb-pkg LOCALVERSION=-linux-surface
8. Install the kernel and headers:
  * $ sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb

### Donations Appreciated!

PayPal: https://www.paypal.me/jakeday42

Bitcoin: 1AH7ByeJBjMoAwsgi9oeNvVLmZHvGoQg68
