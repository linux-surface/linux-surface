# Surface Pro 11 (Intel) Audio Enhancements

Userspace audio configurations for the Surface Pro 11 (Intel Lunar Lake).
Complements the kernel patch `0017-surface-pro-11-intel-sound.patch` which
provides basic RT1320 SoundWire codec support.

## What this provides

| File | What it does |
|------|-------------|
| `rt1320-dmic.conf` | ALSA UCM config for the RT1320 SDCA microphone function (not yet upstream in alsa-ucm-conf) |
| `surface-pro-11-speaker-eq.conf` | PipeWire 8-band speaker EQ that corrects the frequency response of the internal speakers |
| `surface-pro-11-mic-denoise.conf` | PipeWire RNNoise neural noise suppression for the internal microphone |
| `install.sh` | Auto-detecting install/uninstall script (also enables mixer switches) |

### RT1320 DMIC UCM config

The RT1320 codec on the Surface Pro 11 has two SDCA functions: amplifier (AF04,
handled by the upstream `rt1320.conf`) and microphone (AF02). The kernel patch
adds a second DAI for the mic function and remaps the component name to
`rt1320-dmic`, but the corresponding UCM config file is not yet in the upstream
`alsa-ucm-conf` package. Without this file, the internal microphone does not
appear as a capture device.

### SOF topology

The kernel patch sets `sof_tplg_filename = "sof-sdca-1amp-id2.tplg"` for the
Surface Pro 11 machine entry. The actual topology used at runtime is the pair
of SDCA split topologies loaded via the `get_function_tplg_files` callback:

```
sof-audio-pci-intel-lnl: Using function topologies instead intel/sof-ipc4-tplg/sof-sdca-1amp-id2.tplg
sof-audio-pci-intel-lnl: loading topology 0: intel/sof-ipc4-tplg/sof-sdca-1amp-id2.tplg
sof-audio-pci-intel-lnl: loading topology 1: intel/sof-ipc4-tplg/sof-sdca-mic-id4.tplg
```

The `sof_tplg_filename` value is a struct field that the SOF probe path
existence-checks before the function-topology callback ever runs (see
`sound/soc/sof/fw-file-profile.c::sof_test_topology_file`). It must point at
a file that actually ships in firmware-sof-signed / sof-bin —
`sof-sdca-1amp-id2.tplg` is the natural choice because it is also one of the
two function topologies loaded at runtime, so it doubles as a sensible
fallback if `get_function_tplg_files` ever returns 0.

Earlier revisions of this driver used a descriptive monolithic filename
(`sof-lnl-rt1320-l0.tplg`) and relied on the existence check being lenient.
That worked on v6.17 but fails on v6.18+, where the strict existence check
makes the SOF probe error out with `sof_probe_work failed err: -2` before
the function topologies ever get a chance to load.

### Mixer switches

The RT1320 codec's output and capture switches default to off after the driver
loads. Without enabling them, speakers and mic appear in desktop audio settings
but produce no sound:

```bash
# Speaker output (handled by upstream rt1320.conf UCM profile)
amixer -c <N> cset name='rt1320-1 OT23 L Switch' on
amixer -c <N> cset name='rt1320-1 OT23 R Switch' on

# Microphone capture (handled by rt1320-dmic.conf in this package)
amixer -c <N> cset name='rt1320-1 FU Capture Switch' on,on,on,on
```

The install script automatically enables these and persists them with
`alsactl store`. UCM profiles should also handle this, but the explicit
initialization ensures audio works even on the very first boot.

### Speaker EQ

Creates a virtual PipeWire sink ("Surface Pro 11 Speaker (EQ)") that applies
biquad IIR frequency correction before routing to the hardware speaker. Without
it, the speakers sound thin and harsh due to uncorrected enclosure resonances.

### Mic denoise (optional)

Creates a virtual PipeWire source ("Surface Pro 11 Mic (Denoised)") using
RNNoise. Requires an additional package (see below).

## Prerequisites

- Kernel with `0017-surface-pro-11-intel-sound.patch` applied (speakers must work)
- PipeWire (tested with 1.4.7+)
- PipeWire filter-chain module (`libpipewire-module-filter-chain.so`)
- For mic denoise only: `librnnoise_ladspa.so` LADSPA plugin.
  The install script will detect if it's missing and offer to install it:
  - Fedora: prompts to run `dnf install noise-suppression-for-voice`
  - Arch: directs to AUR (`yay -S noise-suppression-for-voice`)
  - Any distro: offers to clone and build from source automatically
    (requires `git`, `cmake`, `make`, `gcc`)
  - Source: https://github.com/werman/noise-suppression-for-voice

## Installation

```bash
sudo ./install.sh
systemctl --user restart filter-chain
```

The script auto-detects your speaker sink and microphone source names from
PipeWire. If detection fails, it falls back to the standard Surface Pro 11
device names.

## Verification

```bash
# Check the filter-chain service is running
systemctl --user status filter-chain

# Look for the virtual EQ sink under "Filters"
wpctl status

# Optionally set the EQ sink as default output
wpctl set-default <node-id-from-wpctl-status>
```

## Uninstallation

```bash
sudo ./install.sh --remove
systemctl --user restart filter-chain
```

## How the speaker EQ works

The Surface Pro 11's internal speakers have a non-flat frequency response due
to the thin tablet enclosure. On Windows, Microsoft applies an 8-stage biquad
IIR equalizer (via SurfaceAPO) to compensate. This config replicates that
correction in PipeWire's filter-chain.

The EQ stages are:
- **Bands 1-2**: High-pass at ~35 Hz — removes subsonic content the speakers
  can't reproduce (prevents distortion from excursion-limited drivers)
- **Bands 3-8**: Mid and high frequency corrections — flattens resonance peaks
  and fills response dips specific to the enclosure

Audio flows: Application -> Virtual EQ Sink -> biquad processing -> Hardware Speaker

## Notes on EQ coefficients

The biquad coefficients (b0, b1, b2, a1, a2 per band) were extracted from
`SurfaceAPO_3070.json`, Microsoft's Surface Audio Framework configuration for
this device (SUBSYS_307010EC). These are numerical IIR filter parameters
representing the measured acoustic correction curve for the speaker enclosure.

No binary files from Windows are used — only the mathematical filter
coefficients.

## Manual configuration

If you prefer not to use the install script, copy the files manually:

```bash
# UCM config (required for microphone to appear)
sudo cp rt1320-dmic.conf /usr/share/alsa/ucm2/sof-soundwire/

# Enable mixer switches (required on first boot or if UCM hasn't activated them)
CARD=$(grep -l sofsoundwire /proc/asound/card*/id 2>/dev/null | head -1 | grep -oP 'card\K[0-9]+')
sudo amixer -c "$CARD" cset name='rt1320-1 OT23 L Switch' on
sudo amixer -c "$CARD" cset name='rt1320-1 OT23 R Switch' on
sudo amixer -c "$CARD" cset name='rt1320-1 FU Capture Switch' on,on,on,on
sudo alsactl store "$CARD"

# PipeWire filter-chain configs
sudo mkdir -p /etc/pipewire/filter-chain.conf.d
sudo cp surface-pro-11-speaker-eq.conf /etc/pipewire/filter-chain.conf.d/
sudo cp surface-pro-11-mic-denoise.conf /etc/pipewire/filter-chain.conf.d/
```

Then edit the `target.object` lines in each PipeWire config to match your device
names (find them with `wpctl status`).

## Upstream status

| Component | Upstream | Status |
|-----------|----------|--------|
| `rt1320-dmic.conf` | [alsa-ucm-conf](https://github.com/alsa-project/alsa-ucm-conf) | Not yet submitted |
| Kernel RT1320 mic DAI | linux kernel `sound/soc/sdw_utils/` | Not yet submitted |
| Speaker EQ | N/A (device-specific PipeWire config) | linux-surface contrib |
| Mic denoise | N/A (generic RNNoise wrapper) | linux-surface contrib |

## Technical notes

### Why `sof_tplg_filename` is `sof-sdca-1amp-id2.tplg`

The kernel patch sets `sof_tplg_filename = "sof-sdca-1amp-id2.tplg"` in the
machine table entry. This is a real file shipped in firmware-sof-signed /
sof-bin and is also one of the two function topologies loaded at runtime.

`sof_tplg_filename` is a **required struct field** — it is dereferenced
unconditionally in several places in the SOF core (`pcm.c`, `hda.c`,
`fw-file-profile.c`). On v6.18+ the probe path also *existence-checks* it
before the `get_function_tplg_files` callback runs:

```
sof_init_environment()
  └── sof_test_topology_file()    // firmware_request_nowarn() → -ENOENT if missing
                                  // probe aborts before topology load
```

So the value must be a file that actually exists on disk. The function
topology callback still takes priority during the real load:

```
topology.c::snd_sof_load_topology()
  if (get_function_tplg_files returns > 0)
      → load per-function topologies (sof-sdca-1amp-id2.tplg + sof-sdca-mic-id4.tplg)
  else
      → fall back to sof_tplg_filename (single file)
```

Using `sof-sdca-1amp-id2.tplg` as the field value gives us both:
the existence check passes, and if the callback ever returns 0 the fallback
still loads a working amp topology (just without the mic).

Earlier revisions of this patch used a descriptive monolithic name
(`sof-lnl-rt1320-l0.tplg`) the way other upstream entries do, e.g.
`sof-lnl-cs42l43-l0.tplg`, `sof-lnl-rt722-l0.tplg`, etc. Those names rely on
historical loose-existence behaviour and only work for codec configurations
where the SOF project actually ships a matching tplg file. For SP11's
RT1320-on-link-0 configuration no such file ships, so we use the function
topology name directly.

### Why the `sofsoundwire` UCM alias is created

The SOF SoundWire machine driver names its card `sof-soundwire`, but ALSA
strips non-alphanumeric characters from `card.id`, so the actual id exposed
in `/proc/asound/cardN/id` is `sofsoundwire` (no dash). UCM matches profiles
by `card.id` via `conf.d/<card-id>/`, and upstream alsa-ucm-conf only ships
`conf.d/sof-soundwire/`. Without a `conf.d/sofsoundwire/` alias, WirePlumber
can't find a UCM profile and the internal speaker/mic don't appear in
PipeWire — even though `aplay -l` sees the card.

The install script creates `conf.d/sofsoundwire/sofsoundwire.conf` as a
symlink back to the upstream `sof-soundwire.conf`. The right long-term fix
is a PR against alsa-project/alsa-ucm-conf to add the alias upstream so it
covers every Lunar Lake / Panther Lake SDW device, not just SP11.
