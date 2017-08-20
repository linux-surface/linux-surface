#!/bin/sh
#
# mwifiex_pcie: handle the flakey wifi

set -eu

case "$1" in
pre)
	ifdown --force mlan0 || true
	modprobe -r mwifiex_pcie
	;;
post)
	echo 1 > /sys/bus/pci/devices/0000\:02\:00.0/reset
	modprobe mwifiex_pcie
	;;
esac

exit 0
