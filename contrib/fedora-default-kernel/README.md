# Fedora Default Kernel Updater

This is a small systemd service and path unit that makes sure that the linux-surface kernel is
always booted by default on Fedora.

The out of the box behaviour is that the newest kernel will be booted by default. This means that
if the default kernel is ahead of the surface kernel, you have to go into GRUB and change the
selection every time.

## Installation

```bash
$ sudo dnf install /usr/sbin/grubby
$ sudo cp default-kernel.{path,service} /etc/systemd/system/
$ sudo systemctl daemon-reload
$ sudo systemctl enable --now default-kernel.path
```
