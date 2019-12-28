#!/bin/bash

LX_BASE=""
LX_VERSION=""

if [ -r "/etc/os-release" ]; then
    . "/etc/os-release"
    if [ "$ID" = "arch" ]; then
        LX_BASE="$ID"
    elif [ "$ID" = "ubuntu" ]; then
        LX_BASE="$ID"
        LX_VERSION=$VERSION_ID
    elif [ -n "$UBUNTU_CODENAME" ] ; then
        LX_BASE="ubuntu"
        LX_VERSION="$VERSION_ID"
    else
        LX_BASE="$ID"
        LX_VERSION="$VERSION"
    fi
else
    echo "Could not identify your distro. Please open script and run commands manually."
    exit
fi

SUR_MODEL="$(dmidecode | grep "Product Name" -m 1 | xargs | sed -e 's/Product Name: //g')"

echo "Running $LX_BASE version $LX_VERSION on a $SUR_MODEL."
read -rp "Press enter if this is correct, or CTRL-C to cancel." tmp; echo
echo "Continuing setup..."

echo "==> Copying the config files under root to where they belong..."
for dir in $(ls root/); do cp -Rbv "root/$dir/"* "/$dir/"; done
echo

echo "==> Copying firmware files under root..."
cp -rv firmware/* /lib/firmware/
echo

echo "==> Making /lib/systemd/system-sleep/sleep executable..."
chmod -v a+x /lib/systemd/system-sleep/sleep

printf "
Suspend is recommended over hibernate. If you chose to use hibernate, please
make sure you've setup your swap file per the instructions in the README.\n\n"
read -rp "Do you want to replace suspend with hibernate? (type yes or no): " \
    usehibernate;echo

if [ "$usehibernate" = "yes" ]; then
    if [ "$LX_BASE" = "ubuntu" ] && [ 1 -eq "$(echo "${LX_VERSION} >= 17.10" | bc)" ]; then
        echo "==> Using Hibernate instead of Suspend..."
        ln -vsfb /lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target
        sudo ln -vsfb /lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
    else
        echo "==> Using Hibernate instead of Suspend..."
        ln -vsfb /usr/lib/systemd/system/hibernate.target /etc/systemd/system/suspend.target
        sudo ln -vsfb /usr/lib/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
    fi
else
    echo "==> Not touching Suspend"
fi

printf "
Patched libwacom packages are available to better support the pen. If you
intend to use the pen, it's recommended that you install them!\n\n"
read -rp "Do you want to install the patched libwacom packages? (type yes or no): " \
    uselibwacom;echo

if [ "$uselibwacom" = "yes" ]; then
    echo "==> Downloading latest libwacom-surface..."

    urls=$(curl --silent "https://api.github.com/repos/linux-surface/libwacom-surface-deb/releases/latest" \
           | tr ',' '\n' \
           | grep '"browser_download_url":' \
           | sed -E 's/.*"([^"]+)".*/\1/' \
           | grep '.deb$')

    wget -P tmp $urls

    echo "==> Installing latest libwacom-surface..."

    dpkg -i tmp/*.deb
    rm -rf tmp
else
    echo "==> Not touching libwacom"
fi

printf "
This repo comes with example xorg and pulse audio configs. If you chose to
keep them, be sure to rename them and uncomment out what you'd like to
keep!\n\n"
read -rp "Do you want to remove the example intel xorg config? (type yes or no): " \
    removexorg;echo

if [ "$removexorg" = "yes" ]; then
    echo "==> Removing the example intel xorg config..."
    rm -v /etc/X11/xorg.conf.d/20-intel_example.conf
else
    echo "==> Not touching example intel xorg config" \
         "(/etc/X11/xorg.conf.d/20-intel_example.conf)"
fi

echo
read -rp "Do you want to remove the example pulse audio config files? (type yes or no): "\
        removepulse;echo

if [ "$removepulse" = "yes" ]; then
    echo "==> Removing the example pulse audio config files..."
    rm -v /etc/pulse/daemon_example.conf
    rm -v /etc/pulse/default_example.pa
else
    echo "==> Not touching example pulse audio config files" \
         "    (/etc/pulse/*_example.*)"
fi

if [ "$SUR_MODEL" = "Surface Pro 3" ]; then
    echo "==> Remove unneeded udev rules for Surface Pro 3..."
    rm -v /etc/udev/rules.d/98-keyboardscovers.rules
fi

if [ "$SUR_MODEL" = "Surface Go" ]; then
    if [ ! -f "/etc/init.d/surfacego-touchscreen" ]; then
        echo "==> Patching power control for Surface Go touchscreen..."
        echo "echo \"on\" > /sys/devices/pci0000:00/0000:00:15.1/i2c_designware.1/power/control" \
            > /etc/init.d/surfacego-touchscreen
        chmod -v 755 /etc/init.d/surfacego-touchscreen
        update-rc.d surfacego-touchscreen defaults
    fi
fi

read -rp "
Do you want to set your clock to local time instead of UTC? This fixes
issues when dual booting with Windows. (type yes or no): " \
    uselocaltime;echo

if [ "$uselocaltime" = "yes" ]; then
    echo "==> Setting clock to local time..."
    timedatectl set-local-rtc 1
    hwclock --systohc --localtime
else
    echo "==> Not setting clock"
fi

read -rp "
Do you want this script to download and install the latest kernel for you?
(type yes or no): " autoinstallkernel;echo

if [ "$autoinstallkernel" = "yes" ]; then
    echo "==> Downloading latest kernel..."

    urls=$(curl --silent "https://api.github.com/repos/linux-surface/linux-surface/releases/latest" \
           | tr ',' '\n' \
           | grep '"browser_download_url":' \
           | sed -E 's/.*"([^"]+)".*/\1/' \
           | grep '.deb$')

    wget -P tmp $urls

    echo
    echo "==> Installing latest kernel..."

    dpkg -i tmp/*.deb
    rm -rf tmp
else
    echo "==> Not downloading latest kernel"
fi

echo
echo "All done! Please reboot."
