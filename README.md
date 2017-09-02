# Linux Surface

Linux running on the Surface Book and Surface Pro 4. Following the instructions below to install the latest kernel and config files.


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
* Cameras (partial support)
* Suspend/Hibernate
* Sensors (accelerometer, gyroscope, ambient light sensor)
* Battery Readings
* Docking/Undocking Tablet and Keyboard (for Surface Book)

### What's NOT Working

* Dedicated GPU (if you have a performance base on a Surface Book, otherwise onboard works fine)
* Cameras (not fully supported yet)

### Download Pre-built Kernel, Headers and IPTS_firmware

Downloads for ubuntu based distros (other distros will need to compile from source in the kernel folder):

https://goo.gl/QSZCwq

You will need to download :
 - ipts_firmware.zip
 - linux_image-*
 - linux_header-*

### Instructions

1. Copy the files under root to where they belong:
  * $ sudo cp -R root/* /
2. Extract ipts_firmware.zip to /lib/firmware/intel/ipts/
  * $ sudo mkdir -p /lib/firmware/intel/ipts
  * $ unzip ipts_firmware.zip 
  * $ sudo mv ipts_firmware/* /lib/firmware/intel/ipts/
3. Fix issue with Suspend to Disk:
  * $ sudo ln -s /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -s /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
4. Install the custom kernel and headers:
  * $ sudo dpkg -i linux-image*dev linux-headers*deb
5. Reboot on installed kernel.

### Donations Appreciated!

PayPal: contact for details!

Bitcoin: 1JkpbAJ41W6SUjH9vCRDpHNNpecjPK3Zid
