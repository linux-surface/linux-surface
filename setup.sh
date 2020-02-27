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

echo "WARNING: This script doesn't automatically install packages anymore."
echo "Please add a package repository and install the packages yourself!"
echo "https://github.com/linux-surface/linux-surface/wiki/Installation-and-Setup#surface-kernel-installation"
echo
echo "You should also check the post installation notes:"
echo "https://github.com/linux-surface/linux-surface/wiki/Installation-and-Setup#post-installation"
echo
echo "If you want to install the packages manually instead:"
echo
echo "Install the patched kernel:"
echo "- SL3/SP7: Use the latest release."
echo "- Other devices: Use the latest 4.19/5.3 release if you want multi-touch"
echo "  support. 5.5 only supports pen and single-touch."
echo "  https://github.com/linux-surface/linux-surface/releases"
echo
echo "Install the IPTS firmware package:"
echo "  https://github.com/linux-surface/surface-ipts-firmware/releases"
echo
echo "Patched libwacom packages are available to better support the pen."
echo "If you intend to use the pen, it's recommended that you install them!"
echo "  https://github.com/linux-surface/libwacom-surface-deb/releases"
