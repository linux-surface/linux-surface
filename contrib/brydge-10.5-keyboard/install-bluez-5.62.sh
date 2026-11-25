#!/bin/bash
#
# Install BlueZ 5.62 from the Debian snapshot archive to work around a
# trackpad regression in BlueZ ≥ 5.64 that affects the Brydge 10.5 Go+
# keyboard (and other HID-over-GATT devices that present keyboard + mouse
# + trackpad as a composite HID descriptor).
#
# Symptom: on Ubuntu 22.04's stock bluez 5.64, the keyboard keys work but
# the trackpad does NOT deliver cursor events — the input device shows
# up in /dev/input/eventN but reports zero relative motion. Downgrading
# the bluez daemon to 5.62 restores trackpad functionality.
#
# We pull the .debs from snapshot.debian.org (which archives every Debian
# package version ever) because Ubuntu 22.04 never shipped 5.62 — Debian
# did, in bullseye-backports / bookworm around late 2021.
#
# This is a deliberately simple install: download, install via apt, hold.
# apt-mark hold prevents jammy-updates from silently reinstalling 5.64 the
# next time you run `apt upgrade`.
#
# To reverse: sudo apt-mark unhold bluez libbluetooth3 bluez-obexd && \
#             sudo apt install --reinstall bluez libbluetooth3 bluez-obexd

set -euo pipefail

TS="20211227T152656Z"   # snapshot.debian.org timestamp where 5.62-2 lives
BASE="https://snapshot.debian.org/archive/debian/${TS}/pool/main/b/bluez"
PKGS=(
    "bluez_5.62-2_amd64.deb"
    "bluez-obexd_5.62-2_amd64.deb"
    "libbluetooth3_5.62-2_amd64.deb"
)
WORK="$(mktemp -d /tmp/bluez-5.62.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

if [[ $EUID -ne 0 ]]; then
    echo "ERROR: must run as root (use sudo)" >&2
    exit 1
fi

echo "==> Downloading BlueZ 5.62-2 .debs from snapshot.debian.org"
for p in "${PKGS[@]}"; do
    echo "  - $p"
    curl -sSLf --retry 3 -o "$WORK/$p" "$BASE/$p"
done

echo "==> Installing"
apt-get install -y --allow-downgrades \
    "$WORK/libbluetooth3_5.62-2_amd64.deb" \
    "$WORK/bluez_5.62-2_amd64.deb" \
    "$WORK/bluez-obexd_5.62-2_amd64.deb"

echo "==> Holding bluez packages to prevent automatic upgrade"
apt-mark hold bluez libbluetooth3 bluez-obexd

echo "==> Restarting bluetoothd"
systemctl restart bluetooth.service

echo "==> Installed versions:"
dpkg -l bluez libbluetooth3 bluez-obexd 2>&1 | awk '/^[hi]/ {printf "  %-20s %s\n", $2, $3}'
echo
echo "==> Daemon version:"
/usr/libexec/bluetooth/bluetoothd --version 2>&1 || \
    /usr/lib/bluetooth/bluetoothd --version 2>&1 || \
    echo "  (could not locate bluetoothd binary)"
echo
echo "Done. Reconnect your Brydge 10.5 Go+ — the trackpad should now work."
