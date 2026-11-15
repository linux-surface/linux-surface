# Speaker EQ for Surface Pro 5 and Surface Pro 7+

The internal speakers on both devices sound thin and boxy by default. On
Windows, Microsoft and Dolby apply speaker correction in the driver. On Linux,
the speakers get the raw signal. This adds a PipeWire filter-chain EQ that
does the same kind of correction, using only PipeWire's built-in filters
(no extra plugins, no EasyEffects).

| File | Device |
|---|---|
| `surface-pro-5.conf` | Surface Pro (5th Gen) / Surface Pro 2017 |
| `surface-pro-7-plus.conf` | Surface Pro 7+ |
| `install-speaker-eq.sh` | Detects the model and the speaker output, then installs and enables the EQ |

## What the EQ does

Each config chains six filters (values are for Pro 5 / Pro 7+):

- **Pre-gain (x0.56 / x0.53)**: reduces the input level so the boosts
  below cannot clip.
- **High-pass (140 Hz / 110 Hz)**: cuts frequencies these tiny drivers
  can't reproduce. Bass below this only makes them distort and rattle at
  high volume. Removing it is the biggest improvement in loudness and clarity.
- **Body boost (+4 dB @ 220 Hz / +4.5 dB @ 180 Hz)**: adds warmth just
  above the drivers' lowest usable frequency.
- **Mud cut (-2.5 dB @ ~450 Hz)**: removes the "boxy" resonance of the
  chassis.
- **Harshness cut (-2 dB @ 3.2 / 3.6 kHz)**: makes high volume less
  fatiguing.
- **Presence and air boosts**: bring back vocal clarity and detail.

These values are a tuned starting point, not a lab measurement of your unit.
Each value is on one line in the `.conf` file. To change a value, edit the
installed copy and run `systemctl --user restart pipewire`.

## Installation

Requires PipeWire (default on current Arch-based distros, Fedora, Ubuntu
22.10+, and Mint 22+) and `pactl`.

```sh
./install-speaker-eq.sh            # run as your user, not root
```

The script detects the device, finds the internal speaker sink, writes
`~/.config/pipewire/pipewire.conf.d/60-surface-speaker-eq.conf`, restarts
PipeWire and makes "Surface Pro … Speakers (EQ)" the default output.

To install manually, copy the right `.conf` file to
`~/.config/pipewire/pipewire.conf.d/`. Replace `SPEAKER_SINK_NAME` with the
speaker sink name from `pactl list short sinks`, for example
`alsa_output.pci-0000_00_1f.3.analog-stereo`.

Remove with `./install-speaker-eq.sh --uninstall`.

## Notes

- The EQ is tuned for speakers only. On these devices the headphone jack is
  often the same ALSA sink as the speakers. When you use wired headphones,
  pick the plain (non-EQ) output in your sound settings.
- If the speakers crackle at full volume, lower the `Mult` pre-gain value
  (for example to `0.45`).
- If you want more bass and accept some distortion, lower the high-pass
  `Freq` value by 10–20 Hz.
