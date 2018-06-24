#!/bin/sh

LX_BASE=""
LX_VERSION=""

if [ -r /etc/os-release ]; then
    . /etc/os-release
	if [ $ID = arch ]; then
		LX_BASE=$ID
    elif [ $ID = ubuntu ]; then
		LX_BASE=$ID
		LX_VERSION=$VERSION_ID
	elif [ ! -z "$UBUNTU_CODENAME" ] ; then
		LX_BASE="ubuntu"
		LX_VERSION=$VERSION_ID
    else
		LX_BASE=$ID
		LX_VERSION=$VERSION
    fi
else
    echo "Could not identify your distro. Please open script and run commands manually."
	exit
fi

SUR_MODEL="$(dmidecode | grep "Product Name" -m 1 | xargs | sed -e 's/Product Name: //g')"
SUR_SKU="$(dmidecode | grep "SKU Number" -m 1 | xargs | sed -e 's/SKU Number: //g')"

echo "\nRunning $LX_BASE version $LX_VERSION on a $SUR_MODEL.\n"

read -rp "Press enter if this is correct, or CTRL-C to cancel." cont;echo

echo "\nContinuing setup...\n"

echo "Coping the config files under root to where they belong...\n"
cp -R root/* /

echo "Making /lib/systemd/system-sleep/sleep executable...\n"
chmod a+x /lib/systemd/system-sleep/sleep

read -rp "Do you want to replace suspend with hibernate? (type yes or no) " usehibernate;echo

if [ "$usehibernate" = "yes" ]; then
	if [ "$LX_BASE" = "ubuntu" ] && [ 1 -eq "$(echo "${LX_VERSION} >= 17.10" | bc)" ]; then
		echo "Using Hibernate instead of Suspend...\n"
		ln -sf /lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -sf /lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
	else
		echo "Using Hibernate instead of Suspend...\n"
		ln -sf /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -sf /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
	fi
else
	echo "Not touching Suspend\n"
fi

read -rp "Do you want use the patched libwacom packages? (type yes or no) " uselibwacom;echo

if [ "$uselibwacom" = "yes" ]; then
	echo "Installing patched libwacom packages..."
		dpkg -i packages/libwacom/*.deb
		apt-mark hold libwacom
else
	echo "Not touching libwacom"
fi

if [ "$SUR_MODEL" = "Surface Pro 3" ]; then
	echo "\nInstalling i915 firmware for Surface Pro 3...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_bxt.zip -d /lib/firmware/i915/
fi

if [ "$SUR_MODEL" = "Surface Pro" ]; then
	echo "\nInstalling IPTS firmware for Surface Pro 2017...\n"
	mkdir -p /lib/firmware/intel/ipts
	unzip -o firmware/ipts_firmware_v102.zip -d /lib/firmware/intel/ipts/

	echo "\nInstalling i915 firmware for Surface Pro 2017...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_kbl.zip -d /lib/firmware/i915/
fi

if [ "$SUR_MODEL" = "Surface Pro 4" ]; then
	echo "\nInstalling IPTS firmware for Surface Pro 4...\n"
	mkdir -p /lib/firmware/intel/ipts
	unzip -o firmware/ipts_firmware_v78.zip -d /lib/firmware/intel/ipts/

	echo "\nInstalling i915 firmware for Surface Pro 4...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_skl.zip -d /lib/firmware/i915/
fi

if [ "$SUR_MODEL" = "Surface Pro 2017" ]; then
	echo "\nInstalling IPTS firmware for Surface Pro 2017...\n"
	mkdir -p /lib/firmware/intel/ipts
	unzip -o firmware/ipts_firmware_v102.zip -d /lib/firmware/intel/ipts/

	echo "\nInstalling i915 firmware for Surface Pro 2017...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_kbl.zip -d /lib/firmware/i915/
fi

if [ "$SUR_MODEL" = "Surface Laptop" ]; then
	echo "\nInstalling IPTS firmware for Surface Laptop...\n"
	mkdir -p /lib/firmware/intel/ipts
	unzip -o firmware/ipts_firmware_v79.zip -d /lib/firmware/intel/ipts/

	echo "\nInstalling i915 firmware for Surface Laptop...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_skl.zip -d /lib/firmware/i915/
fi

if [ "$SUR_MODEL" = "Surface Book" ]; then
	echo "\nInstalling IPTS firmware for Surface Book...\n"
	mkdir -p /lib/firmware/intel/ipts
	unzip -o firmware/ipts_firmware_v76.zip -d /lib/firmware/intel/ipts/

	echo "\nInstalling i915 firmware for Surface Book...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_skl.zip -d /lib/firmware/i915/
fi

if [ "$SUR_MODEL" = "Surface Book 2" ]; then
	echo "\nInstalling IPTS firmware for Surface Book 2...\n"
	mkdir -p /lib/firmware/intel/ipts
	if [ "$SUR_SKU" = "Surface_Book_1793" ]; then
		unzip -o firmware/ipts_firmware_v101.zip -d /lib/firmware/intel/ipts/
	else
		unzip -o firmware/ipts_firmware_v137.zip -d /lib/firmware/intel/ipts/
	fi

	echo "\nInstalling i915 firmware for Surface Book 2...\n"
	mkdir -p /lib/firmware/i915
	unzip -o firmware/i915_firmware_kbl.zip -d /lib/firmware/i915/

	echo "\nInstalling nvidia firmware for Surface Book 2...\n"
	mkdir -p /lib/firmware/nvidia/gp108
	unzip -o firmware/nvidia_firmware_gp108.zip -d /lib/firmware/nvidia/gp108/
fi

echo "Installing marvell firmware...\n"
mkdir -p /lib/firmware/mrvl/
unzip -o firmware/mrvl_firmware.zip -d /lib/firmware/mrvl/

read -rp "Do you want to set your clock to local time instead of UTC? This fixes issues when dual booting with Windows. (type yes or no) " uselocaltime;echo

if [ "$uselocaltime" = "yes" ]; then
	echo "Setting clock to local time...\n"

	timedatectl set-local-rtc 1
	hwclock --systohc --localtime
else
	echo "Not setting clock"
fi

read -rp "Do you want this script to download and install the latest kernel for you? (type yes or no) " autoinstallkernel;echo

if [ "$autoinstallkernel" = "yes" ]; then
	echo "Downloading latest kernel...\n"

	urls=$(curl --silent "https://api.github.com/repos/jakeday/linux-surface/releases/latest" | grep '"browser_download_url":' | sed -E 's/.*"([^"]+)".*/\1/')

	resp=$(wget -P tmp $urls)

	echo "Installing latest kernel...\n"

	dpkg -i tmp/*.deb
	rm -rf tmp
else
	echo "Not downloading latest kernel"
fi

echo "\nAll done! Please reboot."
