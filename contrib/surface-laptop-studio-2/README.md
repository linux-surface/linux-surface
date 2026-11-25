# Surface Laptop Studio 2 Tweaks

Optional configuration tweaks and workarounds for the **Microsoft Surface Laptop Studio 2** (some also apply to the original Surface Laptop Studio).

## 1. `s2idle` Suspend Hang Workaround (NVIDIA + `ithc`)

See [`linux-surface/linux-surface#1014`](https://github.com/linux-surface/linux-surface/issues/1014). On SLS2 units with the discrete NVIDIA GPU (`s2idle`-only, no ACPI S3), `ithc` re-initialization can race NVIDIA's suspend/resume path and hang the system during `PM: suspend entry (s2idle)`.

Two files mitigate this:

1. **Unbind/rebind `ithc` across sleep (`50-ithc-unbind.sh`)**

   Verify your THC PCI device addresses first (`lspci -k | grep -A3 "Touch Host Controller"` — on SLS2 they are `0000:00:10.6` and `0000:00:10.7`), adjust `ITHC_PCI` inside `50-ithc-unbind.sh` if needed, then install:

   ```bash
   sudo install -m 0755 50-ithc-unbind.sh /etc/systemd/system-sleep/50-ithc-unbind.sh
   ```

2. **Enable NVIDIA S0ix power management (`nvidia-s0ix.conf`)**

   Placed in a separate `/etc/modprobe.d/` file that sorts after distro-packaged NVIDIA modprobe files so driver updates do not overwrite it:

   ```bash
   sudo install -m 0644 nvidia-s0ix.conf /etc/modprobe.d/nvidia-s0ix.conf
   ```

   Regenerate your initramfs if your distribution embeds modprobe configs (`sudo update-initramfs -u` or `sudo dracut --force`) and reboot. Verify `S0ix Power Management: Status: Enabled` in `/proc/driver/nvidia/gpus/*/power`.

## 2. Optional Custom `libinput` Touchpad Acceleration & Scroll Curve (`touchpad-accel-shim/`)

`libinput >= 1.23` supports `LIBINPUT_ACCEL_PROFILE_CUSTOM` curves, which allow fine control over slow-speed precision and fast-flick acceleration plus scroll speed.

- **GNOME 51+:** Supports custom pointer acceleration profiles natively via `gsettings` / Mutter; you do not need this shim.
- **GNOME < 51 (e.g., GNOME 46–50):** Exposes only `flat` and `adaptive` in settings. `touchpad-accel-shim/` provides a minimal `LD_PRELOAD` shim scoped strictly to `org.gnome.Shell@.service` (via a user systemd drop-in, rather than global `/etc/ld.so.preload`) that applies an S-curve tuned for `IPTSD Virtual Touchpad 045E:0C46` (`~85 units/mm`).

Build and install:

```bash
cd touchpad-accel-shim
make
sudo install -m 0755 touchpad-accel.so /usr/local/lib/touchpad-accel.so
sudo install -m 0644 touchpad-accel.conf /etc/touchpad-accel.conf
mkdir -p ~/.config/systemd/user/org.gnome.Shell@.service.d
install -m 0644 org.gnome.Shell@.service.d-touchpad-accel.conf \
  ~/.config/systemd/user/org.gnome.Shell@.service.d/touchpad-accel.conf
systemctl --user daemon-reload
```

Log out and back in to activate. Edit `/etc/touchpad-accel.conf` to customize `motion_points` / `scroll_points` (note that `scroll_points` must stay configured whenever `LIBINPUT_ACCEL_PROFILE_CUSTOM` is active, otherwise `libinput` defaults scroll gain to `1.0 px/unit`).
