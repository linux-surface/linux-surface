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
echo "Please download and install them from the releases page!"
echo ""
echo "Patched libwacom packages are available to better support the pen."
echo "If you intend to use the pen, it's recommended that you install them!"
echo "  https://github.com/linux-surface/libwacom-surface-deb/releases"
echo
echo "Install the patched kernel:"
echo "  https://github.com/linux-surface/linux-surface/releases"
echo
echo "- SL3/SP7: Use the latest 5.4 release."
echo "- Other devices: Use the latest 5.3 release if you want touchscreen"
echo "  support. It's currently broken on 5.4."
