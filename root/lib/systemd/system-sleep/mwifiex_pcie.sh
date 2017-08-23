#!/bin/sh
#
# mwifiex_pcie: handle the flakey wifi

set -eu

case "$1" in
pre)
	ifdown --force wlp3s0 || true
	modprobe -r mwifiex_pcie
	;;
post)
	echo 1 > /sys/bus/pci/devices/0000\:02\:00.0/reset
	modprobe mwifiex_pcie
	ifup --force wlp3s0 || true
	;;
esac

exit 0
