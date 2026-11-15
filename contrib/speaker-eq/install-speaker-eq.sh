#!/usr/bin/env bash
#
# install-speaker-eq.sh - install the PipeWire speaker EQ for
# Surface Pro 5 or Surface Pro 7+. Run as your normal user, NOT with sudo.
#
# Usage:
#   ./install-speaker-eq.sh                      auto-detect model and speaker sink
#   ./install-speaker-eq.sh --model pro5|pro7plus
#   ./install-speaker-eq.sh --sink <node.name>   use this sink as the speakers
#   ./install-speaker-eq.sh --uninstall

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/pipewire/pipewire.conf.d"
DEST="$CONF_DIR/60-surface-speaker-eq.conf"
EQ_SINK="effect_input.surface_speaker_eq"

MODEL=""
SINK=""
UNINSTALL=0
while [ $# -gt 0 ]; do
    case "$1" in
        --model) MODEL="$2"; shift 2;;
        --sink) SINK="$2"; shift 2;;
        --uninstall) UNINSTALL=1; shift;;
        -h|--help) sed -n '3,11p' "$0"; exit 0;;
        *) echo "unknown option: $1" >&2; exit 1;;
    esac
done

if [ "$(id -u)" = 0 ]; then
    echo "Run this as your normal user (PipeWire config is per-user)." >&2
    exit 1
fi

restart_pipewire() {
    systemctl --user restart pipewire pipewire-pulse wireplumber 2>/dev/null \
        || echo "Could not restart PipeWire; log out and back in."
    sleep 2
}

if [ "$UNINSTALL" = 1 ]; then
    rm -f "$DEST"
    restart_pipewire
    echo "Removed $DEST"
    exit 0
fi

if [ -z "$MODEL" ]; then
    PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null || true)"
    SKU="$(cat /sys/class/dmi/id/product_sku 2>/dev/null || true)"
    case "$PRODUCT" in
        "Surface Pro 7+") MODEL=pro7plus;;
        "Surface Pro")
            # The 2017 Surface Pro (5th gen) reports just "Surface Pro".
            case "$SKU" in Surface_Pro_17*) MODEL=pro5;; esac;;
    esac
    if [ -z "$MODEL" ]; then
        echo "Could not detect a Surface Pro 5 or Pro 7+ (product: '$PRODUCT', sku: '$SKU')." >&2
        echo "Pass --model pro5 or --model pro7plus." >&2
        exit 1
    fi
fi
case "$MODEL" in
    pro5) SRC="$SCRIPT_DIR/surface-pro-5.conf";;
    pro7plus) SRC="$SCRIPT_DIR/surface-pro-7-plus.conf";;
    *) echo "unknown model: $MODEL (use pro5 or pro7plus)" >&2; exit 1;;
esac

if ! command -v pactl >/dev/null 2>&1; then
    echo "pactl not found; install pipewire-pulse / pulseaudio-utils (it's used to find the speaker sink)." >&2
    exit 1
fi

if [ -z "$SINK" ]; then
    # The internal audio is the PCH HDA/SOF device at 00:1f.3. Prefer a sink
    # that looks like speakers, then any sink on that device.
    SINKS="$(pactl list short sinks | awk '{print $2}' | grep -v '^effect_' || true)"
    SINK="$(printf '%s\n' "$SINKS" | grep '00_1f\.3' | grep -iE 'speaker|analog-stereo|HiFi' | head -n 1 || true)"
    [ -z "$SINK" ] && SINK="$(printf '%s\n' "$SINKS" | grep '00_1f\.3' | head -n 1 || true)"
    if [ -z "$SINK" ]; then
        echo "Could not find the internal speaker sink. Available sinks:" >&2
        printf '  %s\n' "$SINKS" >&2
        echo "Re-run with --sink <name>." >&2
        exit 1
    fi
fi

mkdir -p "$CONF_DIR"
sed "s|SPEAKER_SINK_NAME|$SINK|" "$SRC" > "$DEST"
echo "Installed $(basename "$SRC") -> $DEST"
echo "Speaker sink: $SINK"

restart_pipewire

if pactl list short sinks | grep -q "$EQ_SINK"; then
    pactl set-default-sink "$EQ_SINK"
    echo "EQ is active and set as the default output."
    echo "For headphones or Bluetooth, just pick that output; to compare, switch"
    echo "between the '(EQ)' output and the plain speakers in your sound settings."
else
    echo "The EQ sink did not appear. Check: journalctl --user -u pipewire -b" >&2
    exit 1
fi
