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

### What's NOT Working

* Dedicated GPU (if you have a performance base on a Surface Book, otherwise onboard works fine)
* Cameras (not fully supported yet)

### Instructions

1. Copy the files under root to where they belong:
  * $ sudo cp root/* /
2. Extract ipts_firmware.zip to /lib/firmware/intel/ipts/
  * $ sudo mkdir -p /lib/firmware/intel/ipts
  * $ sudo unzip ipts_firmware.zip /lib/firmware/intel/ipts/
3. Install the custom kernel and headers:
  * $ sudo dpkg -i linux-image*dev linux-headers*deb

### Download Pre-built Kernel and Headers

Downloads for ubuntu based distros (other distros will need to compile from source in the kernel folder):

https://goo.gl/QSZCwq

### Donations Appreciated!

PayPal: contact for details!

Bitcoin: 1JkpbAJ41W6SUjH9vCRDpHNNpecjPK3Zid
