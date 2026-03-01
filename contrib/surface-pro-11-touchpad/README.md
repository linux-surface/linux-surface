# Surface Pro 11 (Intel) Type Cover Touchpad Fixes

Userspace configuration files to complement the kernel patch
`0020-surface-pro-11-intel-touchpad.patch`.

## Installation

```bash
# udev rule: enables disable-while-typing (DWT) by grouping keyboard + touchpad
sudo cp 99-surface-touchpad-group.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=input

# libinput quirk: edge palm detection tuning
sudo mkdir -p /etc/libinput
sudo cp local-overrides.quirks /etc/libinput/
```

Log out and back in (or reboot) for full effect.

## What these fix

| File | Problem | Fix |
|------|---------|-----|
| `99-surface-touchpad-group.rules` | DWT (disable-while-typing) doesn't work because keyboard and touchpad are in different libinput device groups | Groups them together via `LIBINPUT_DEVICE_GROUP` |
| `local-overrides.quirks` | No pressure data from touchpad → limited palm detection | Tunes edge-based palm threshold |

## Notes

- The kernel patch (hid-multitouch.c) fixes right-click and click+drag by
  setting `INPUT_PROP_BUTTONPAD`. The firmware misreports Pad Type as
  PRESSUREPAD (1) instead of CLICKPAD (0).
- `045E:0C8B` (keyboard with consumer controls) is deliberately excluded from
  the device group — its `REL_HWHEEL` capability triggers libinput's
  disable-while-trackpointing and kills the touchpad.
