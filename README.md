# Linux Surface

Linux running on the Microsoft Surface devices.
Follow the instructions below to install the latest kernel.

[Announcements and Updates](https://github.com/linux-surface/linux-surface/issues/96) | [Upstream Status](https://github.com/linux-surface/linux-surface/issues/205)

### Why / About this Project

These days, Linux supports a lot of devices out-of-the-box.
As a matter of fact, this includes a good portion of the Microsoft Surface devices—for most parts at least.
So why would you need a special kernel for Surface devices?
In short, for the parts that are not supported upstream yet.

Unfortunately, Surface devices tend to be a bit special.
This is mostly because some hardware choices Microsoft made are rarely (if at all) used by other, more "standard", devices.
For example:
- Surface devices (4th generation and later) use their own embedded controller (the Surface Aggregator Module, or SAM).
  In contrast to other devices, however, some newer Surface devices route their keyboard and touchpad input via this controller.
  Unfortunately, every new Surface device requires some (usually small) patch to enable support for it, since devices managed by SAM are generally not auto-discoverable.
- Surface devices (4th generation and later, excluding the Go series) use a rather special system for touch and pen input.
  In short, this requires user-space processing of touch and pen data to enable multitouch support and has not been upstreamed yet.
- Surface devices rely on Intel's ISP for camera image processing.
  This means that the webcam also requires some user-space processing.
  While patches are being upstreamed, not all devices are supported (even with this project), and more work remains to be done.

We aim to send all the changes we make here upstream, but this may take time.
This kernel allows us to ship new features faster, as we do not have to adhere to the upstream release schedule (and, for better or worse, code standards).
We also rely on it to test and prototype patches before sending them upstream, which is crucial because we maintainers cannot test on all Surface devices (which also means we may break things along the way).

_So should you install this custom kernel and the associated packages?_
It depends: We generally recommend you try your standard distribution kernel first.
If that works well for you, great!
But if you're missing any features or experiencing issues, take a look at our [feature matrix](https://github.com/linux-surface/linux-surface/wiki/Supported-Devices-and-Features#feature-matrix) and give our kernel and packages a try.
If your device is not listed as supported yet, feel free to open an issue.

### Supported Devices

* Surface Book
* Surface Book 2
* Surface Book 3
* Surface 3
* Surface Go
* Surface Go 2
* Surface Go 3
* Surface Laptop
* Surface Laptop 2
* Surface Laptop 3
* Surface Laptop 4
* Surface Laptop 5
* Surface Laptop 6
* Surface Laptop Go
* Surface Laptop Go 2
* Surface Laptop Go 3
* Surface Laptop Studio
* Surface Laptop Studio 2
* Surface Pro 1
* Surface Pro 3
* Surface Pro 4
* Surface Pro (5th Gen) / Surface Pro 2017
* Surface Pro 6
* Surface Pro 7
* Surface Pro 7+
* Surface Pro 8
* Surface Pro 9
* Surface Pro 10
* Surface Studio

### Features / What's Working

See the [feature matrix](https://github.com/linux-surface/linux-surface/wiki/Supported-Devices-and-Features#feature-matrix) for more information about each device.

### Disclaimer

* For the most part, things are tested on a Surface Book 2.
  While most things are reportedly fully working on other devices, your mileage may vary.
  Please look at the issues list for possible exceptions.

## Installation and Setup

We provide package repositories for the patched kernel and other utilities.
Please refer to the [detailed installation and setup guide][wiki-setup].
There, you may also find device-specific caveats.
In case you have disk encryption set up or plan to use it, take care to follow the respective instructions in the installation guide and have a look at the respective [wiki page][wiki-encryption].
After installation, you may want to have a look at the [wiki][wiki] and the `contrib/` directory for useful tweaks.

If you want to compile the kernel yourself (e.g. if your distribution is not supported), please have a look at the [wiki][wiki-compiling].

## Additional Information

### Notes

* If you are getting stuck at boot when loading the ramdisk, you need to install the Processor Microcode Firmware for Intel CPUs (usually found under Additional Drivers in Software and Updates).
* Using TLP can cause slowdowns, laggy performance, and occasional hangs if not configured properly! You have been warned.
* If you want to use hibernate instead of suspend, you need to create a swap partition or file, please follow your distribution's instructions (or [here][hibernate-setup]).

### Support

If you have questions or need support, please join our [Matrix Space][matrix-space]!
This space contains
- a [support channel][matrix-support] for general support and
- a [development channel][matrix-development] for all development related questions and discussions.

## License
This repository contains patches, which are either derivative work targeting a specific already licensed source, i.e. parts of the Linux kernel, or introduce new parts to the Linux kernel.
These patches fall thus, if not explicitly stated otherwise, under the license of the source they are targeting, or if they introduce new code, the license they explicitly specify inside of the patch.
Please refer to the specific patch and source in question for further information.
License texts can be obtained at https://github.com/torvalds/linux/tree/master/LICENSES.

[wiki]: https://github.com/linux-surface/linux-surface/wiki
[wiki-setup]: https://github.com/linux-surface/linux-surface/wiki/Installation-and-Setup
[wiki-compiling]: https://github.com/linux-surface/linux-surface/wiki/Compiling-the-Kernel-from-Source
[wiki-encryption]: https://github.com/linux-surface/linux-surface/wiki/Disk-Encryption

[matrix-space]: https://matrix.to/#/#linux-surface:matrix.org
[matrix-support]: https://matrix.to/#/#linux-surface-support:matrix.org
[matrix-development]: https://matrix.to/#/#linux-surface-development:matrix.org

[hibernate-setup]: https://fitzcarraldoblog.wordpress.com/2018/07/14/configuring-lubuntu-18-04-to-enable-hibernation-using-a-swap-file
[releases]: https://github.com/linux-surface/linux-surface/releases

[linux-surface-kernel]: https://github.com/linux-surface/kernel/
