# IPTS (touch + stylus) on SB3 surface-ipu4

## Symptom

Touchscreen / Surface Pen dead on `7.3.0-surface-ipu4+`: no `IPTSD Virtual Touchscreen`, `iptsd@dev-hidraw*` inactive.

## Cause

`CONFIG_HID_IPTS is not set` in the shipped image. Without `ipts.ko` there is no IPTS hidraw, so iptsd never starts.

That happened when an older build re-applied linux-surface `0005-ipts` onto `v7.3-surface` (IPTS already in-tree): `drivers/misc/mei/pci-me.c` rejected, and the Fedora-derived `.config` never enabled `HID_IPTS`.

## Proper fix

Rebuild with current `kernel/build-and-install-kernel.sh` (ensures IPTS sources + `CONFIG_HID_IPTS=m`).

## Temporary OOT fallback (already used on this SB3)

```bash
# sources: copy of drivers/hid/ipts → /usr/local/src/ipts-oot
sudo bash contrib/sb3-ipu4-cameras/tools/rebuild-ipts-module.sh
# or, if ipts.ko is already built under /tmp/ipts-oot:
sudo bash contrib/sb3-ipu4-cameras/tools/fix-ipts-touch.sh
```

Loads `/lib/modules/$(uname -r)/extra/ipts/ipts.ko` and `/etc/modules-load.d/ipts.conf`.
After a Plasma session that started without the digitizer, log out/in once.
