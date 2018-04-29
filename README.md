# Linux Surface

Linux running on the Surface Book, Surface Book 2, Surface Pro 3, Surface Pro 4, Surface Pro 2017 and Surface Laptop. Follow the instructions below to install the latest kernel and config files.

### What's Working

* Keyboard (and backlight) (not yet working for Surface Laptop)
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
* Surface Docks
* DisplayPort
* USB-C (including for HDMI Out)
* Dedicated Nvidia GPU (Surface Book 2)

### What's NOT Working

* Dedicated Nvidia GPU (if you have a performance base on a Surface Book 1, otherwise onboard works fine)
* Cameras (not fully supported yet)
* Suspend (uses Connected Standby which is not supported yet)

### Disclaimer
* For the most part, things are tested on a Surface Book. While most things are reportedly fully working on other devices, your mileage may vary. Please look at the issues list for possible exceptions.

### Download Pre-built Kernel and Headers

Downloads for ubuntu based distros (other distros will need to compile from source using the included patches):

https://github.com/jakeday/linux-surface/releases

You will need to download the image, headers and libc-dev deb files for the version you want to install.

### Instructions

0. (Prep) Install Git:
  ```
   sudo apt install git
  ```
1. Clone the linux-surface repo:
  ```
   git clone https://github.com/jakeday/linux-surface.git ~/linux-surface
  ```
2. Change directory to linux-surface repo:
  ```
   cd ~/linux-surface
  ```
3. Run setup script:
  ```
   sudo sh setup.sh
  ```
4. Install the headers, kernel and libc-dev (make sure you cd to your download location first):
  ```
  sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb linux-libc-dev-[VERSION].deb
  ```
5. Reboot on installed kernel.

### Compiling the Kernel from Source

If you don't want to use the pre-built kernel and headers, you can compile the kernel yourself following these steps:

0. (Prep) Install the required packages for compiling the kernel:
  ```
  sudo apt install build-essential binutils-dev libncurses5-dev libssl-dev ccache bison flex
  ```
1. Assuming you cloned the linux-surface repo (this one) into ~/linux-surface, go to the parent directory:
  ```
  cd ~
  ```
2. Clone the mainline stable kernel repo:
  ```
  git clone git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
  ```
3. Go into the linux-stable directory:
  ```
  cd linux-stable
  ```
4. Checkout the version of the kernel you wish to target (replacing with your target version):
  ```
  git checkout v4.y.z
  ```
5. Apply the kernel patches from the linux-surface repo (this one):
  ```
  for i in ~/linux-surface/patches/[VERSION]/*.patch; do patch -p1 < $i; done
  ```
6. Get current config file and patch it:
  ```
  cat /boot/config-$(uname -r) > .config
  patch -p1 < ~/linux-surface/patches/config.patch
  ```
7. Compile the kernel and headers (for ubuntu, refer to the build guild for your distro):
  ```
  make -j \`getconf _NPROCESSORS_ONLN\` deb-pkg LOCALVERSION=-linux-surface
  ```
8. Install the headers, kernel and libc-dev:
  ```
  sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb linux-libc-dev-[VERSION].deb
  ```

### NOTES

* If you are getting stuck at boot when loading the ramdisk, you need to install the Processor Microcode Firmware for Intel CPUs (usually found under Additional Drivers in Software and Updates).
* If you are having issues with the position of the cursor matching the pen/stylus, you'll need to update your libwacom as mentioned here: https://github.com/jakeday/linux-surface/issues/46

### Donations Appreciated!

PayPal: https://www.paypal.me/jakeday42

Bitcoin: 1AH7ByeJBjMoAwsgi9oeNvVLmZHvGoQg68
