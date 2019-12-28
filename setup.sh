#!/bin/sh

# https://gist.github.com/davejamesmiller/1965569
ask() {
    local prompt default reply

    if [ "${2:-}" = "Y" ]; then
        prompt="Y/n"
        default=Y
    elif [ "${2:-}" = "N" ]; then
        prompt="y/N"
        default=N
    else
        prompt="y/n"
        default=
    fi

    while true; do

        # Ask the question (not using "read -p" as it uses stderr not stdout)
        echo -n "$1 [$prompt]: "

        # Read the answer (use /dev/tty in case stdin is redirected from somewhere else)
        read reply </dev/tty

        # Default?
        if [ -z "$reply" ]; then
            reply=$default
        fi

        # Check if the reply is valid
        case "$reply" in
            Y*|y*) return 0 ;;
            N*|n*) return 1 ;;
        esac

    done
}

echo "==> Copying the config files under root to where they belong..."
for dir in $(ls root/); do
    sudo cp -Rbv "root/$dir/"* "/$dir/"
done

echo "==> Copying firmware files under root..."
sudo cp -rv firmware/* /lib/firmware/

echo "==> Making /lib/systemd/system-sleep/sleep executable..."
sudo chmod -v a+x /lib/systemd/system-sleep/sleep

echo "==> Enabling power management for Surface Go touchscreen..."
sudo systemctl enable -q surfacego-touchscreen

echo

echo "Suspend is recommended over hibernate. If you chose to use"
echo "hibernate, please make sure you've setup your swap file per"
echo "the instructions in the README."

if ask "Do you want to replace suspend with hibernate?" N; then
    echo "==> Using Hibernate instead of Suspend..."
    if [ -f "/usr/lib/systemd/system/hibernate.target" ]; then
        LIB="/usr/lib"
    else
        LIB="/lib"
    fi

    sudo ln -vsfb $LIB/systemd/system/hibernate.target /etc/systemd/system/suspend.target
    sudo ln -vsfb $LIB/systemd/system/systemd-hibernate.service /etc/systemd/system/systemd-suspend.service
else
    echo "==> Not touching Suspend"
fi

echo

echo "Setting your clock to local time can fix issues with Windows dualboot."
if ask "Do you want to set your clock to local time instead of UTC?" N; then
    echo "==> Setting clock to local time..."
    sudo timedatectl set-local-rtc 1
    sudo hwclock --systohc --localtime
else
    echo "==> Not setting clock..."
fi

echo

echo "Patched libwacom packages are available to better support the pen."
echo "If you intend to use the pen, it's recommended that you install them!"

if ask "Do you want to install the patched libwacom?" Y; then
    echo "==> Downloading latest libwacom-surface..."

    urls=$(curl --silent "https://api.github.com/repos/linux-surface/libwacom-surface-deb/releases/latest" \
           | tr ',' '\n' \
           | grep '"browser_download_url":' \
           | sed -E 's/.*"([^"]+)".*/\1/' \
           | grep '.deb$')

    wget -P tmp $urls

    echo "==> Installing latest libwacom-surface..."

    sudo dpkg -i tmp/*.deb
    rm -rf tmp
else
    echo "==> Not touching libwacom"
fi

echo

if ask "Do you want to download and install the latest kernel?" Y; then
    echo "==> Downloading latest kernel..."

    urls=$(curl --silent "https://api.github.com/repos/linux-surface/linux-surface/releases/latest" \
           | tr ',' '\n' \
           | grep '"browser_download_url":' \
           | sed -E 's/.*"([^"]+)".*/\1/' \
           | grep '.deb$')

    wget -P tmp $urls

    echo
    echo "==> Installing latest kernel..."

    sudo dpkg -i tmp/*.deb
    rm -rf tmp
else
    echo "==> Not downloading latest kernel"
fi

echo

echo "All done! Please reboot."
