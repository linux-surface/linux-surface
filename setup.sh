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

echo "Copying the config files under root to where they belong...\n"
for dir in $(ls root/); do cp -Rb root/$dir/* /$dir/; done

echo "Making /lib/systemd/system-sleep/sleep executable...\n"
chmod a+x /lib/systemd/system-sleep/sleep

echo "Suspend is recommended over hibernate. If you chose to use hibernate, please make sure you've setup your swap file per the instructions in the README.\n"
read -rp "Do you want to replace suspend with hibernate? (type yes or no) " usehibernate;echo

if [ "$usehibernate" = "yes" ]; then
	if [ "$LX_BASE" = "ubuntu" ] && [ 1 -eq "$(echo "${LX_VERSION} >= 17.10" | bc)" ]; then
		echo "Using Hibernate instead of Suspend...\n"
		ln -sfb /lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -sfb /lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
	else
		echo "Using Hibernate instead of Suspend...\n"
		ln -sfb /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target && sudo ln -sfb /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
	fi
else
	echo "Not touching Suspend\n"
fi

echo "Patched libwacom packages are available to better support the pen. If you intend to use the pen, it's recommended that you install them!\n"

read -rp "Do you want to install the patched libwacom packages? (type yes or no) " uselibwacom;echo

if [ "$uselibwacom" = "yes" ]; then
	echo "Installing patched libwacom packages..."
		dpkg -i packages/libwacom/*.deb
		apt-mark hold libwacom
else
	echo "Not touching libwacom"
fi

echo "This repo comes with example xorg and pulse audio configs. If you chose to keep them, be sure to rename them and uncomment out what you'd like to keep!\n"

read -rp "Do you want to remove the example intel xorg config? (type yes or no) " removexorg;echo

if [ "$removexorg" = "yes" ]; then
	echo "Removing the example intel xorg config..."
		rm /etc/X11/xorg.conf.d/20-intel_example.conf
else
	echo "Not touching example intel xorg config (/etc/X11/xorg.conf.d/20-intel_example.conf)"
fi

read -rp "Do you want to remove the example pulse audio config files? (type yes or no) " removepulse;echo

if [ "$removepulse" = "yes" ]; then
	echo "Removing the example pulse audio config files..."
		rm /etc/pulse/daemon_example.conf
		rm /etc/pulse/default_example.pa
else
	echo "Not touching example pulse audio config files (/etc/pulse/*_example.*)"
fi

if [ "$SUR_MODEL" = "Surface Pro 3" ]; then
	echo "\nRemove unneeded udev rules for Surface Pro 3...\n"
	rm /etc/udev/rules.d/98-keyboardscovers.rules
fi

if [ "$SUR_MODEL" = "Surface Go" ]; then
	if [ ! -f "/etc/init.d/surfacego-touchscreen" ]; then
		echo "\nPatching power control for Surface Go touchscreen...\n"
		echo "echo \"on\" > /sys/devices/pci0000:00/0000:00:15.1/i2c_designware.1/power/control" > /etc/init.d/surfacego-touchscreen
		chmod 755 /etc/init.d/surfacego-touchscreen
		update-rc.d surfacego-touchscreen defaults
	fi
fi

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

	urls=$(curl --silent "https://api.github.com/repos/jakeday/linux-surface/releases/latest" | tr ',' '\n' | grep '"browser_download_url":' | sed -E 's/.*"([^"]+)".*/\1/')

	resp=$(wget -P tmp $urls)

	echo "Installing latest kernel...\n"

	dpkg -i tmp/*.deb
	rm -rf tmp
else
	echo "Not downloading latest kernel"
fi

echo "\nAll done! Please reboot."
