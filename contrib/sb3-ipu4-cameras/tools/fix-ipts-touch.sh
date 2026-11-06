#!/usr/bin/env bash
set -euo pipefail
KVER="$(uname -r)"
SRC="/tmp/ipts-oot/ipts.ko"
DEST="/lib/modules/${KVER}/extra/ipts"

if [[ ! -f "$SRC" ]]; then
  echo "Brak zbudowanego modułu: $SRC"
  exit 1
fi

mkdir -p "$DEST"
install -m 644 "$SRC" "$DEST/ipts.ko"
depmod -a "$KVER"
modprobe -r ipts 2>/dev/null || true
modprobe ipts
sleep 1

echo "=== moduł ==="
lsmod | grep -E '^ipts' || { echo "ipts nie załadowany"; dmesg | tail -40; exit 1; }

echo "=== dmesg ipts ==="
dmesg | grep -i ipts | tail -20 || true

echo "=== urządzenia ==="
iptsd-foreach || true
systemctl list-units 'iptsd@*' --all --no-pager || true

# upewnij się że iptsd wystartuje jeśli hidraw już jest
for hr in /dev/hidraw*; do
  if iptsd-check-device --quiet "$hr" 2>/dev/null; then
    esc=$(systemd-escape --path "$hr")
    systemctl start "iptsd@${esc}.service" || true
    echo "Start iptsd dla $hr"
  fi
done

echo "=== input ==="
grep -A6 -iE 'touchscreen|stylus|IPT' /proc/bus/input/devices || echo "(jeszcze brak wpisów touch/stylus — sprawdź iptsd)"
echo "Gotowe."
