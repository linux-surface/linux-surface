#!/bin/sh
#
# One-shot automatic screen rotation
#
# If the attached keyboard is switched off, whenever the tablet is
# rotated, it is best to put this command into the panel, because
# hotkeys do not work.
#
# On Cinnamon, the 'Command Launcher' add-on can be used with the
# following settings:
#   Panel Icon:                         rotation-allowed-symbolic
#   Tooltip:                            Set screen rotation
#   Keyboard shortcut:                  unassigned | unassigned
#   Show notification on completion:    NO
#   Command:                            rotate-screen
#   Run as root:                        NO
#   Run in alternate directory:         NO
#
# A possible .desktop file can look like this:
#   [Desktop Entry]
#   Encoding=UTF-8
#   Version=1.0
#   Name=Rotate
#   Comment=Rotate Screen
#   Type=Application
#   Exec=rotate-screen
#   Icon=rotation-allowed-symbolic
#   Terminal=false
#   Categories=Settings
#   StartupNotify=true
# and could be saved in ~/.local/share/applications/rotate-screen.desktop
#
#
# Anchestors and ideas:
#   mildmojo
#   https://gist.github.com/mildmojo/48e9025070a2ba40795c
#   mbinnun:
#   https://gist.github.com/mildmojo/48e9025070a2ba40795c#gistcomment-2694429
#   frgomes:
#   https://github.com/frgomes/bash-scripts/blob/develop/bin/rotate
#
#
# MIT License
#
# Copyright (c) 2021 Stephan Helma
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

GDBUS=gdbus
XINPUT=xinput
XRANDR=xrandr


usage () {
    # Display the usage
    #
    # No options

    cat <<EOF
$(basename $0) [-h|--help] [screen|next|previous|normal|left|inverted|right]

Rotate the screen and all pointing devices (touchscreens, pens, touchpads, ...)

The script can handle the following typical use cases:
1. One-shot rotation (automatic detection):
   You hold the device in the new orientation desired and invoke this script.
   The screen and all pointing devices rotate.
   Usage:
      - Disable automatic screen rotation in the settings for your desktop
        environment.
      - Hold the device in the new orientation and call this script with "$0".
   Pre-requisites:
      Programs: $GDBUS, $XINPUT, $XRANDR
      Hardware: Accelerometer
      Package: iio-sensor-proxy
2. Automatic screen rotation, but pointing devices do not rotate:
   The orientation of the screen is set by the desktop environment, but the
   pointing devices are not.
   Usage:
      - Enable automatic screen rotation in the settings for your desktop
        environment.
      - Hold the device in the new orientation and call this script with
        "$0 screen".
   Pre-requisites:
      Programs: $XINPUT, $XRANDR
      Hardware: --
      Package: --
3. Cycle through screen rotations:
   The orientation of the screen is cycled through normal -> left -> inverted
   -> right -> normal (option "next") or in reversed order (option "previous")
   with each call of this script.
   Usage:
      - Disable automatic screen rotation in the settings for your desktop
        environment.
      - Call this script with "$0 next|previous".
   Pre-requisites:
      Programs: $XINPUT, $XRANDR
      Hardware: --
      Package: --
4. One-shot rotation (manual):
   Independent of the actual orientation of the device, the orientation is
   set to the given orientation.
   Usage:
      - Disable automatic screen rotation in the settings for your desktop
        environment.
      - Call this script with "$0 normal|inverted|left|right".
   Pre-requisites:
      Programs: $XINPUT, $XRANDR
      Hardware: --
      Package: --

Note:
   Sometimes not all pointing devices are rotated. The reason is, that the
   pointing device was not active at the time of rotation, as can happen with
   e.g. bluetooth pens. In that case simply call the script again with the
   same parameter.


Parameters:
   -h, --help     Display this help and exit.
   screen, next, normal, left, inverted, right
                  The new orientation (optional). The default behaviour (no
                  option) is to use the orientation from the built-in
                  accelerometer. If 'screen' is given, use the screen's
                  current orientation. 'next' will switch to the next
                  orientation and any other option switches to the specified
                  orientation, regardless of the device's or the screen's
                  actual orientation.
EOF

}


check_commands () {
    # Check for commands needed
    #
    # $1: The required orientation

    if test $1 = auto; then
        GDBUS=$(which $GDBUS)
        if test -z $GDBUS; then
            echo "Command 'gdbus' not found."
            exit 10
        fi
    fi

    XINPUT=$(which $XINPUT)
    if test -z $XINPUT; then
        echo "Command 'xinput' not found."
        exit 11
    fi

    XRANDR=$(which $XRANDR)
    if test -z $XRANDR; then
        echo "Command 'xrandr' not found."
        exit 12
    fi
}


get_dbus_orientation () {
    # Get the orientation from the DBus
    #
    # No options.

    # DBus to query to get the current orientation
    DBUS="--system --dest net.hadess.SensorProxy --object-path /net/hadess/SensorProxy"

    # Check, if DBus is available
    ORIENTATION=$($GDBUS call $DBUS \
                        --method org.freedesktop.DBus.Properties.Get \
                                 net.hadess.SensorProxy HasAccelerometer)
    if test $? != 0; then
        echo $ORIENTATION
        echo " (Is the 'iio-sensor-proxy' package installed and enabled?)"
        exit 20
    elif test "$ORIENTATION" != "(<true>,)"; then
        echo "No sensor available!"
        echo " (Does the computer has a hardware accelerometer?)"
        exit 21
    fi

    # Get the orientation from the DBus
    ORIENTATION=$($GDBUS call $DBUS \
                        --method org.freedesktop.DBus.Properties.Get \
                        net.hadess.SensorProxy AccelerometerOrientation)

    # Release the DBus
    $GDBUS call --system $DBUS --method net.hadess.SensorProxy.ReleaseAccelerometer > /dev/null

    # Normalize the orientation
    case $ORIENTATION in
        "(<'normal'>,)")
            ORIENTATION=normal
            ;;
        "(<'bottom-up'>,)")
            ORIENTATION=inverted
            ;;
        "(<'left-up'>,)")
            ORIENTATION=left
            ;;
        "(<'right-up'>,)")
            ORIENTATION=right
            ;;
        *)
            echo "Orientation $ORIENTATION unknown!"
            echo " (Known orientations are: normal, bottom-up, left-up and right-up.)"
            exit 22
    esac

    # Return the orientation found
    echo $ORIENTATION
}


get_screen_orientation () {
    # Get the orientation from the current screen orientation
    #
    # $1: The screen

    ORIENTATION=$($XRANDR --current --verbose | grep $1 | cut --delimiter=" " -f6)

    case $ORIENTATION in
        normal|inverted|left|right)
            ;;
        *)
            echo "Current screen orientation $ORIENTATION unknown!"
            exit 23
            ;;
    esac

    # Return the orientation found
    echo $ORIENTATION
}


do_rotate () {
    # Rotate screen and pointers
    #
    # $1: The requested mode (only "screen" gets a special treatment)
    # $2: The new orientation
    # $3: The screen to rotate
    # $4-: The pointers to rotate

    TRANSFORM='Coordinate Transformation Matrix'

    MODE=$1
    shift

    ORIENTATION=$1
    shift

    # Rotate the screen
    if test $MODE != screen; then
        # Only rotate it, if we have not got the orientation from the screen
        $XRANDR --output $1 --rotate $ORIENTATION
    fi
    shift

    # Rotate all pointers
    while test $# -gt 0; do
        case $ORIENTATION in
            normal)
                $XINPUT set-prop $1 "$TRANSFORM" 1 0 0 0 1 0 0 0 1
                ;;
            inverted)
                $XINPUT set-prop $1 "$TRANSFORM" -1 0 1 0 -1 1 0 0 1
                ;;
            left)
                $XINPUT set-prop $1 "$TRANSFORM" 0 -1 1 1 0 0 0 0 1
                ;;
            right)
                $XINPUT set-prop $1 "$TRANSFORM" 0 1 0 -1 0 1 0 0 1
                ;;
        esac
        shift
    done
}


# Process the command line options
MODE=auto
while true; do
    case $1 in
        -h|--help)
            usage
            exit
            ;;
        *)
            if test $# -eq 0; then
                break
            fi
            MODE=$1
            shift
            ;;
    esac
done

# Check, if all commands, which are needed, are available
check_commands $MODE

# Get the display
XDISPLAY=$($XRANDR --current --verbose | grep primary | cut --delimiter=" " -f1)

# Get the tablet's orientation
case $MODE in
    auto)
        ORIENTATION=$(get_dbus_orientation)
        ret=$?
        if test $ret != 0; then
            echo $ORIENTATION
            echo "(To use this script, supply the orientation normal, inverted, left or right on the command line.)"
            exit $ret
        fi
        ;;
    screen)
        ORIENTATION=$(get_screen_orientation $XDISPLAY)
        ret=$?
        if test $ret != 0; then
            echo $ORIENTATION
            exit $ret
        fi
        ;;
    next)
        ORIENTATION=$(get_screen_orientation $XDISPLAY)
        ret=$?
        if test $ret != 0; then
            echo $ORIENTATION
            exit $ret
        fi
        case $ORIENTATION in
            normal)
                ORIENTATION=left
                ;;
            left)
                ORIENTATION=inverted
                ;;
            inverted)
                ORIENTATION=right
                ;;
            right)
                ORIENTATION=normal
                ;;
            *)
                ORIENTATION=normal
                ;;
        esac
        ;;
    previous)
        ORIENTATION=$(get_screen_orientation $XDISPLAY)
        ret=$?
        if test $ret != 0; then
            echo $ORIENTATION
            exit $ret
        fi
        case $ORIENTATION in
            normal)
                ORIENTATION=right
                ;;
            left)
                ORIENTATION=normal
                ;;
            inverted)
                ORIENTATION=left
                ;;
            right)
                ORIENTATION=inverted
                ;;
            *)
                ORIENTATION=normal
                ;;
        esac
        ;;
    normal|inverted|left|right)
        ORIENTATION=$MODE
        ;;
    *)
        echo "Unknown command line parameter orientation $MODE"
        exit 1
esac

# Get all pointers
POINTERS=$($XINPUT | grep slave | grep pointer | sed -e 's/^.*id=\([[:digit:]]\+\).*$/\1/')

# Rotate the screen and pointers
echo "Rotate display $XDISPLAY to $ORIENTATION orientation (Pointers: $(echo $POINTERS | sed 's/\n/ /g'))"
do_rotate $MODE $ORIENTATION $XDISPLAY $POINTERS
