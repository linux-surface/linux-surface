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
* Power Button
* Volume Buttons
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

0. (Prep) Install Dependencies:
  ```
   sudo apt install git curl wget sed
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
4. Reboot on installed kernel.

The setup script will handle installing the latest kernel for you. You can also choose to download any version you want and install yourself:
Install the headers, kernel and libc-dev (make sure you cd to your download location first):
  ```
  sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb linux-libc-dev-[VERSION].deb
  ```

### Compiling the Kernel from Source

If you don't want to use the pre-built kernel and headers, you can compile the kernel yourself following these steps:

0. (Prep) Install the required packages for compiling the kernel:
  ```
  sudo apt install build-essential binutils-dev libncurses5-dev libssl-dev ccache bison flex libelf-dev
  ```
1. Clone the mainline stable kernel repo:
  ```
  git clone git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git ~/linux-stable
  ```
2. Go into the linux-stable directory:
  ```
  cd ~/linux-stable
  ```
3. Checkout the version of the kernel you wish to target (replacing with your target version):
  ```
  git checkout v4.y.z
  ```
4. Apply the kernel patches from the linux-surface repo (this one, and assuming you cloned it to ~/linux-surface):
  ```
  for i in ~/linux-surface/patches/[VERSION]/*.patch; do patch -p1 < $i; done
  ```
5. Get current config file and patch it:
  ```
  cat /boot/config-$(uname -r) > .config
  patch -p1 < ~/linux-surface/patches/config.patch
  ```
6. Compile the kernel and headers (for ubuntu, refer to the build guide for your distro):
  ```
  make -j `getconf _NPROCESSORS_ONLN` deb-pkg LOCALVERSION=-linux-surface
  ```
7. Install the headers, kernel and libc-dev:
  ```
  sudo dpkg -i linux-headers-[VERSION].deb linux-image-[VERSION].deb linux-libc-dev-[VERSION].deb
  ```

### Signing the kernel for Secure Boot

(Instructions are for ubuntu, but should work similar for other distros, if they are using shim
and grub as bootloader.)

Since the most recent GRUB2 update (2.02+dfsg1-5ubuntu1) in Ubuntu, GRUB2 does not load unsigned
kernels anymore, as long as Secure Boot is enabled. Users of Ubuntu 18.04 will be notified during
upgrade of the grub-efi package, that this kernel is not signed and the upgrade will abort.

Thus you have three options to solve this problem:

1. You sign the kernel yourself.
2. You use a signed, generic kernel of your distro.
3. You disable Secure Boot.

Since option two and three are not really viable, these are the steps to sign the kernel yourself:

Instructions adapted from [the Ubuntu Blog](https://blog.ubuntu.com/2017/08/11/how-to-sign-things-for-secure-boot).

1. Create the config to create the signing key, save as mokconfig.cnf:
```
# This definition stops the following lines failing if HOME isn't
# defined.
HOME                    = .
RANDFILE                = $ENV::HOME/.rnd 
[ req ]
distinguished_name      = req_distinguished_name
x509_extensions         = v3
string_mask             = utf8only
prompt                  = no

[ req_distinguished_name ]
countryName             = <YOURcountrycode>
stateOrProvinceName     = <YOURstate>
localityName            = <YOURcity>
0.organizationName      = <YOURorganization>
commonName              = Secure Boot Signing Key
emailAddress            = <YOURemail>

[ v3 ]
subjectKeyIdentifier    = hash
authorityKeyIdentifier  = keyid:always,issuer
basicConstraints        = critical,CA:FALSE
extendedKeyUsage        = codeSigning,1.3.6.1.4.1.311.10.3.6
nsComment               = "OpenSSL Generated Certificate"
```
Adjust all parts with <YOUR*> to your details.

2. Create the public and private key for signing the kernel:
```
openssl req -config ./mokconfig.cnf \
        -new -x509 -newkey rsa:2048 \
        -nodes -days 36500 -outform DER \
        -keyout "MOK.priv" \
        -out "MOK.der"
```

3. Convert the key also to PEM format (mokutil needs DER, sbsign needs PEM):
```
openssl x509 -in MOK.der -inform DER -outform PEM -out MOK.pem
```

4. Enroll the key to your shim installation:
```
sudo mokutil --import MOK.der
```
You will be asked for a password, you will just use it to confirm your key selection in the
next step, so choose any.

5. Restart your system. You will encounter a blue screen of a tool called MOKManager.
Select "Enroll MOK" and then "View key". Make sure it is your key you created in step 2.
Afterwards continue the process and you must enter the password which you provided in
step 4. Continue with booting your system.

6. Verify your key is enrolled via:
```
sudo mokutil --list-enrolled
```

7. Sign your installed kernel (it should be at /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface):
```
sudo sbsign --key MOK.priv --cert MOK.pem /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface --output /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface.signed
```

8. Update your grub-config
```
sudo update-grub
```

9. Reboot your system and select signed kernel. If booting works, you can remove the unsigned kernel:
```
sudo mv /boot/vmlinuz-[KERNEL-VERSION]-surface-linux-surface{.signed,}
sudo update-grub
```

Now your system should run under a signed kernel and upgrading GRUB2 works again. If you want
to upgrade the custom kernel, you can sign the new version easily by following above steps
again from step seven on. Thus BACKUP the MOK-keys (MOK.der, MOK.pem, MOK.priv).

### NOTES

* If you are getting stuck at boot when loading the ramdisk, you need to install the Processor Microcode Firmware for Intel CPUs (usually found under Additional Drivers in Software and Updates).
* Do not install TLP! It can cause slowdowns, laggy performance, and occasional hangs! You have been warned.

### Donations Appreciated!

PayPal: https://www.paypal.me/jakeday42

Bitcoin: 1AH7ByeJBjMoAwsgi9oeNvVLmZHvGoQg68
