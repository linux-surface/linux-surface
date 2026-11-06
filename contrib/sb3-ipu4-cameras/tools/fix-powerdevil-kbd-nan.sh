#!/usr/bin/env bash
# Rebuild Fedora powerdevil with Plasma BUG 525916 fix (hide kbd backlight when max==0).
# Needs: dnf download --source powerdevil; dnf builddep -y powerdevil
set -euo pipefail
WORKDIR="${WORKDIR:-/tmp/powerdevil-fix}"
PATCH_URL="https://invent.kde.org/plasma/powerdevil/-/commit/ac993175917bcf0526112cfd8fa29b61b638fba3.patch"
mkdir -p "$WORKDIR"
cd "$WORKDIR"
[[ -f powerdevil-*.src.rpm ]] || dnf download --source powerdevil
curl -fsSL -o kbd-fix.patch "$PATCH_URL"
rm -rf rpmbuild
mkdir -p rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
rpm -i --define "_topdir $WORKDIR/rpmbuild" powerdevil-*.src.rpm
cp -f kbd-fix.patch rpmbuild/SOURCES/
SPEC=rpmbuild/SPECS/powerdevil.spec
grep -q 'kbd-fix.patch' "$SPEC" || sed -i '/^Source0:/a Patch1000: kbd-fix.patch' "$SPEC"
rpmbuild -bb --define "_topdir $WORKDIR/rpmbuild" "$SPEC"
sudo rpm -Uvh --force rpmbuild/RPMS/x86_64/powerdevil-*.rpm
echo "Restart PowerDevil: systemctl --user restart plasma-powerdevil.service"
