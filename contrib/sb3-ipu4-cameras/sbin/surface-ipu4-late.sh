#!/bin/bash
# Surface Book 3 — IPU4 after the greeter. NEVER rmmod. NEVER systemctl start howdy
# from here (After=late in howdy = deadlock → 90s timeout).
set -uo pipefail
LOG=/var/log/surface-ipu4-late.log
MARKER=/run/surface-howdy
exec >>"$LOG" 2>&1
echo "===== $(date -Iseconds) kernel=$(uname -r) ====="

if ! uname -r | grep -q surface-ipu4; then
  echo "skip: not a surface-ipu4 kernel"
  exit 0
fi

mkdir -p "$MARKER"
echo "starting" >"$MARKER/state"
rm -f "$MARKER/cameras-ready"

for i in $(seq 1 40); do
  if [ -d /sys/devices/platform/INT3455:00/gpiochip0 ] || [ -e /sys/bus/gpio/devices/gpiochip0 ]; then
    echo "gpiochip OK (t=$i)"
    break
  fi
  sleep 0.2
done

for n in 00 01 02; do
  if [ -e /sys/bus/platform/devices/INT3472:$n ] && [ ! -e /sys/bus/platform/drivers/int3472-discrete/INT3472:$n ]; then
    echo INT3472:$n > /sys/bus/platform/drivers/int3472-discrete/bind 2>/dev/null && echo "bind INT3472:$n" || true
  fi
done
sleep 0.3

# SELinux: unlabeled .ko can make kmod return Permission denied. No-op without SELinux.
KVER=$(uname -r)
restorecon -F \
  "/lib/modules/$KVER/kernel/drivers/media/pci/intel/ipu4/intel-ipu4p-isys.ko" \
  "/lib/modules/$KVER/kernel/drivers/media/pci/intel/ipu4/intel-ipu4p.ko" \
  "/lib/modules/$KVER/kernel/drivers/media/i2c/ov5693.ko" \
  /etc/systemd/system/surface-ipu4-late.service \
  /etc/systemd/system/surface-webcam.service 2>/dev/null || true
# Do NOT use modprobe -f: on 6.19.8-surface-ipu4 it returns Exec format error.
# The blacklist stops udev autoload; the same modprobe from late (root) loads without -f.
timeout -k 5 45 /usr/sbin/modprobe intel_ipu4p fw_version_check=0 \
  && echo "parent ok" || echo "parent already/fail"
for i in $(seq 1 80); do
  if [ -d /sys/devices/pci0000:00/0000:00:05.0/intel-ipu4-mmu0/intel-ipu60 ]; then
    echo "ipu60 OK (t=$i)"
    break
  fi
  sleep 0.25
done
# PSYS maps the CPD and writes isp->pkg_dir_dma_addr. Without it BOOT_LOAD
# goes out with address 0 → magic 0xffffffeb, then 0x220. AUTH is in psys_fw_init.
for i in $(seq 1 40); do
  if [ -e /dev/mei0 ]; then
    echo "mei0 OK (t=$i)"
    break
  fi
  sleep 0.25
done
if lsmod | grep -q '^intel_ipu4p_psys '; then
  echo "psys already loaded"
else
  if timeout -k 5 90 /usr/sbin/modprobe intel_ipu4p_psys; then
    echo "psys loaded"
  else
    echo "psys modprobe failed rc=$?"
  fi
fi
if lsmod | grep -q '^intel_ipu4p_isys '; then
  echo "isys already loaded"
else
  if timeout -k 5 60 /usr/sbin/modprobe intel_ipu4p_isys; then
    echo "isys loaded"
  else
    echo "isys modprobe failed rc=$?"
  fi
fi
# This boot only — leftover dmesg from previous boots used to fake "auth done"
auth=0
for i in $(seq 1 50); do
  if journalctl -k -b --no-pager 2>/dev/null | grep -q 'CSE authenticate_run done'; then
    echo "CSE auth done (t=$i)"
    auth=1
    break
  fi
  if journalctl -k -b --no-pager 2>/dev/null | grep -q 'intel-ipu4 intel-ipu: CSE boot_load failed'; then
    echo "CSE boot_load failed this boot (t=$i)"
    break
  fi
  sleep 0.4
done
timeout -k 2 12 /usr/sbin/modprobe ov5693 || echo "ov5693 fail"
timeout -k 2 8 /usr/sbin/modprobe ov8865 ov7251 || true
timeout -k 2 5 /usr/sbin/modprobe dw9719 || true

if [ -e /sys/bus/i2c/devices/i2c-INT347A:00-VCM ] && [ ! -e /sys/bus/i2c/devices/i2c-INT347A:00-VCM/driver ]; then
  echo i2c-INT347A:00-VCM > /sys/bus/i2c/drivers/dw9719/bind 2>/dev/null || true
fi
sleep 0.4

for pair in "ov5693 i2c-INT33BE:00" "ov8865 i2c-INT347A:00" "ov7251 i2c-INT347E:00"; do
  set -- $pair
  drv=$1; dev=$2
  if [ -e /sys/bus/i2c/drivers/$drv ] && [ -e /sys/bus/i2c/devices/$dev ] && \
     [ ! -e /sys/bus/i2c/drivers/$drv/$dev ]; then
    echo $dev > /sys/bus/i2c/drivers/$drv/bind 2>/dev/null || true
  fi
done

ok=0
for i in $(seq 1 30); do
  if [ -e /dev/media0 ] && [ -e /dev/video0 ] && [ -e /dev/video10 ]; then
    ok=1
    break
  fi
  sleep 0.4
done

ls -l /dev/media0 /dev/video0 /dev/video10 2>/dev/null || echo "missing media/video"

if [ "$ok" -eq 1 ] && [ "$auth" -eq 1 ]; then
  echo "cameras ready" >"$MARKER/state"
  echo "ok" >"$MARKER/cameras-ready"
  restorecon -F /dev/media0 /dev/video[0-9]* 2>/dev/null || \
    chcon -t v4l_device_t /dev/media0 /dev/video[0-9]* 2>/dev/null || true
  # Do NOT chmod 666 the whole IPU — browsers used to open video0 (uaccess ACL)
  # and STREAMON of raw CSI hung the display. Loopbacks Front/Back only.
  /usr/local/sbin/surface-cam-perms.sh || true
else
  echo "cameras-timeout nodes=$ok auth=$auth" >"$MARKER/state"
  rm -f "$MARKER/cameras-ready"
fi

# Refresh IR device_path only — never force disabled=true (that broke PAM face auth).
/usr/local/sbin/surface-howdy-gate.sh || true
if [[ "$ok" -eq 1 ]] && systemctl is-active --quiet surface-webcam.service 2>/dev/null; then
  /usr/local/sbin/surface-howdy-gate.sh --enable || true
fi
# Howdy is started by a watch/path unit — not from here (deadlock)
echo "late done ok=$ok auth=$auth"
exit 0
