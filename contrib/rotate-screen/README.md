# One-shot automatic screen rotation

There are many scripts around (you can find some [here](https://gist.github.com/mildmojo/48e9025070a2ba40795c), where this script was published first), which rotate the screen automatically. Many desktop environment have this feature built in, but if you like a one-shot automatic rotation facility, this is for you.


## Motivation

Automatic rotation can be useful, but sometimes you just want to set the screen to an orientation and keep it at that (think: reading in bed). Many desktop environments provide facilities to either continuously adapt the screen to the device's orientation or to select the orientation manually (if it works, e.g. Cinnamon is not able to adapt *all* input devices to the new orientation). Using the manual option you might get confused with the naming of the orientations …

The script provided here takes a different approach: You hold your device in the orientation you would like and call this script. It figures out the current orientation and rotates the screen and *all* input devices accordingly.


## Installation

Copy the script to a place on your computer and make it executable (`chmod +x rotate-screen`).

Since the Typecover keyboard gets disabled, whenever the Surface is rotated, it is advisable to put the script "on your desktop", so that it is just a click away.

Finally, don't forget to switch off automatic screen rotation!

### Cinnamon

For the Cinnamon desktop, one option is to use the [Command Launcher](https://cinnamon-spices.linuxmint.com/applets/view/139) add-on. Install it, add an instance to your panel and set it up as follows:
```
Tooltip:                            Set screen rotation
Keyboard shortcut:                  unassigned | unassigned
Show notification on completion:    NO
Command:                            rotate-screen
Run as root:                        NO
Run in alternate directory:         NO
```
You might have to give the full path to the `rotate-screen` script. A possible icon is `rotation-allowed-symbolic` (or use your own).

### Other desktop environments

If you have a good way, how to call this script from other desktop environments, please make a PR, so that others can benefit from your insight. Thanks.


## Invocation

Hold your Surface in the orientation you like and call the script, e.g. by clicking on the icon installed in the previous step.
