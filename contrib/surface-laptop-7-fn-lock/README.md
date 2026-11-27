# Surface Laptop 7 (13.8-inch and 15-inch): Fn-lock pointer-focus quirk

## Symptom

Press **Fn** once on the integrated keyboard (the Fn LED lights up). Switch to
another window and click it: the click activates the window that had focus
when Fn was pressed instead. Firefox and other apps appear dead to the
pointer, while keyboard input still works. Press **Fn** again (LED off) and
clicks behave normally.

## Cause

On the Surface Laptop 7, the 13.8-inch and 15-inch models use the same SAM HID
keyboard (`Microsoft Surface 045E:006C`). While Fn lock is on, it reports
`SystemFunctionShift`; the kernel maps that to `BTN_0` and holds it for the
whole lock. libinput forwards it as a pointer button, and compositors that
treat a held button as "do not update pointer focus" (KWin does) keep the
stale focus window. This is the same mechanism as libinput issue #1251 / KDE
bug 515288 on the Surface Laptop 3.

The existing `Microsoft Surface *Keyboard` libinput quirk already drops
`BTN_0`, but its `MatchName` requires the word `Keyboard`, which the SAM HID
name does not contain.

## Fix

This directory contains an example libinput local override. Merge it into
your existing `/etc/libinput/local-overrides.quirks`; do not overwrite that
file if it already has entries:

```bash
sudo mkdir -p /etc/libinput
sudo tee -a /etc/libinput/local-overrides.quirks < surface-laptop-7-fn-lock.quirks  # from this directory
```

If the section is already present, do not add it twice. Then log out and back
in (or restart the compositor) so libinput re-reads the quirks; a reboot also
works. Verify by toggling Fn and clicking another window. Pressing Fn again is
the immediate workaround and releases `BTN_0`.

`EVIOCGKEY` and `libinput debug-events` may still show the kernel `BTN_0`
state; the override filters libinput's delivery, not the kernel event, so
verify behaviourally.

Both the 13.8-inch and 15-inch models are covered: they share the same
keyboard. The 15-inch was tested for the KWin behaviour; on the 13.8-inch it
is verified that libinput applies the override, but the KWin behaviour was not
tested there.

## Upstream

The proper fix is a section in libinput's
`quirks/30-vendor-microsoft.quirks` for this SAM HID name. This contrib entry
is a local workaround until libinput ships it.

* libinput issue #1251: <https://gitlab.freedesktop.org/libinput/libinput/-/issues/1251>
* KDE bug 515288: <https://bugs.kde.org/show_bug.cgi?id=515288>
