#!/usr/bin/env bash
# Surface Book 3 RGB + IR (Howdy) webcams — distro-neutral installer.
#  1) build kernel 6.19.8-surface-ipu4 if it is not already running
#  2) install firmware, userspace, systemd, udev, loopbacks, Howdy helpers
#  3) after reboot on surface-ipu4: Front / Back / IR loopbacks via webcamd
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/opt/surface-cameras}"
INSTALL_USER="${SUDO_USER:-${USER}}"
INSTALL_HOME="$(getent passwd "$INSTALL_USER" | cut -d: -f6)"
INSTALL_UID="$(id -u "$INSTALL_USER")"

if [[ $(id -u) -ne 0 ]]; then
  echo "Run: sudo $0" >&2
  exit 1
fi

pkg_install() {
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y \
      python3 python3-opencv v4l-utils v4l2loopback gcc make git git-lfs \
      curl tar xz bc openssl-devel elfutils-libelf-devel dwarves flex bison \
      ncurses-devel rsync acl howdy || true
  elif command -v apt-get >/dev/null 2>&1; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
      python3 python3-opencv v4l-utils v4l2loopback-dkms gcc make git git-lfs \
      curl tar xz-utils bc libssl-dev libelf-dev dwarves flex bison \
      libncurses-dev rsync acl || true
    apt-get install -y howdy 2>/dev/null || true
  elif command -v pacman >/dev/null 2>&1; then
    pacman -Sy --noconfirm --needed \
      python python-opencv v4l-utils v4l2loopback-dkms gcc make git git-lfs \
      curl tar xz bc openssl libelf pahole flex bison ncurses rsync acl || true
  elif command -v zypper >/dev/null 2>&1; then
    zypper --non-interactive install \
      python3 python3-opencv v4l-utils v4l2loopback gcc make git git-lfs \
      curl tar xz bc libopenssl-devel libelf-devel dwarves flex bison \
      ncurses-devel rsync acl || true
  else
    echo "Unknown package manager. Install python3, opencv, v4l-utils," >&2
    echo "v4l2loopback, gcc, make, git, git-lfs and kernel build deps." >&2
    exit 1
  fi
}

echo "=== 1. packages ==="
pkg_install

echo "=== 2. userspace → $PREFIX ==="
install -d "$PREFIX/tools" "$PREFIX/lib" "$PREFIX/sbin"
install -m 644 "$ROOT/userspace/lib/"*.py "$PREFIX/lib/"
install -m 755 "$ROOT/userspace/tools/surface_webcamd.py" "$PREFIX/tools/"
if [[ -f "$ROOT/userspace/tools/raw_hold.c" ]]; then
  install -m 644 "$ROOT/userspace/tools/raw_hold.c" "$PREFIX/tools/"
  cc -O2 -Wall -o "$PREFIX/tools/raw_hold" "$PREFIX/tools/raw_hold.c"
  chmod 755 "$PREFIX/tools/raw_hold"
fi
if [[ -d "$ROOT/userspace/sbin" ]]; then
  install -m 755 "$ROOT/userspace/sbin/"* "$PREFIX/sbin/" 2>/dev/null || true
fi

echo "=== 3. sbin / bin / desktop / systemd / udev / modprobe / wireplumber ==="
install -d /usr/local/sbin /usr/local/bin /usr/share/applications
# shell helpers
install -m 755 "$ROOT/sbin/"*.sh /usr/local/sbin/
# Howdy PAM helpers (no .sh suffix)
[[ -f "$ROOT/sbin/howdy-gtk-pam-safe" ]] && \
  install -m 755 "$ROOT/sbin/howdy-gtk-pam-safe" /usr/local/bin/howdy-gtk
[[ -f "$ROOT/bin/surface-howdy-add-profile" ]] && \
  install -m 755 "$ROOT/bin/surface-howdy-add-profile" /usr/local/bin/surface-howdy-add-profile
[[ -f "$ROOT/desktop/howdy-add-profile.desktop" ]] && \
  install -m 644 "$ROOT/desktop/howdy-add-profile.desktop" /usr/share/applications/howdy-add-profile.desktop
command -v update-desktop-database >/dev/null && update-desktop-database /usr/share/applications >/dev/null 2>&1 || true

install -m 644 "$ROOT/systemd/"*.service /etc/systemd/system/
install -m 644 "$ROOT/udev/"*.rules /etc/udev/rules.d/
install -d /etc/modprobe.d /etc/modules-load.d
install -m 644 "$ROOT/modprobe.d/"*.conf /etc/modprobe.d/
install -m 644 "$ROOT/modules-load.d/"*.conf /etc/modules-load.d/
install -d /etc/wireplumber/wireplumber.conf.d
install -m 644 "$ROOT/wireplumber/"*.conf /etc/wireplumber/wireplumber.conf.d/
# OBS 98-v4l2loopback.conf overwrites card labels — disable it
ln -sfn /dev/null /etc/modprobe.d/98-v4l2loopback.conf

# SELinux Howdy V4L (Fedora / RHEL) — optional
if command -v checkmodule >/dev/null && [[ -f "$ROOT/selinux/howdy_v4l.te" ]]; then
  install -d /usr/local/share/selinux
  checkmodule -M -m -o /tmp/howdy_v4l.mod "$ROOT/selinux/howdy_v4l.te" && \
    semodule_package -o /usr/local/share/selinux/howdy_v4l.pp -m /tmp/howdy_v4l.mod && \
    semodule -i /usr/local/share/selinux/howdy_v4l.pp 2>/dev/null || \
    semodule -u /usr/local/share/selinux/howdy_v4l.pp 2>/dev/null || true
fi

ENV_BODY="HOME=$INSTALL_HOME
XDG_RUNTIME_DIR=/run/user/$INSTALL_UID
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$INSTALL_UID/bus
SURFACE_WEBCAM_IR=1
"
# Debian/Ubuntu: /etc/default ; Fedora/RHEL/openSUSE: /etc/sysconfig
if [[ -d /etc/sysconfig ]]; then
  printf '%s' "$ENV_BODY" >/etc/sysconfig/surface-webcam
fi
install -d /etc/default
printf '%s' "$ENV_BODY" >/etc/default/surface-webcam

if getent group video >/dev/null; then
  usermod -aG video "$INSTALL_USER" || true
fi

systemctl daemon-reload
systemctl enable surface-ipu4-late.service surface-cam-perms.service surface-webcam.service
systemctl enable surface-howdy-enable.service 2>/dev/null || true
systemctl disable surface-howdy-gate.service 2>/dev/null || true
udevadm control --reload-rules || true

echo "=== 4. kernel surface-ipu4 ==="
if uname -r | grep -q surface-ipu4; then
  echo "already on $(uname -r) — skip compile"
  "$ROOT/kernel/install-firmware.sh" || true
else
  echo "Stock linux-surface has no IPU4P. Building 6.19.8-surface-ipu4…"
  "$ROOT/kernel/build-and-install-kernel.sh"
  echo
  echo "Reboot and select the kernel with *surface-ipu4* in the bootloader."
  echo "After reboot the cameras start on their own (late + webcamd)."
  echo "Then: sudo configure-howdy-ir.sh && surface-howdy-add-profile"
  exit 0
fi

modprobe v4l2loopback 2>/dev/null || true
systemctl restart surface-ipu4-late.service || true
systemctl restart surface-cam-perms.service || true
systemctl restart surface-webcam.service || true
systemctl start surface-howdy-enable.service || true

# Howdy INI + PAM (safe: no-model exits in <1s → password)
if command -v howdy >/dev/null 2>&1 && [[ -x /usr/local/sbin/configure-howdy-ir.sh ]]; then
  /usr/local/sbin/configure-howdy-ir.sh || true
fi

echo
echo "Done. Front / Back / IR (Surface-*-Howdy) via webcamd."
echo "Add face: Applications → Howdy — Add Face Profile  (or surface-howdy-add-profile)"
echo "Docs: $ROOT/HOWDY.md"
