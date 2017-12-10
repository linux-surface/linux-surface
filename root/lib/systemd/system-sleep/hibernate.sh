#!/bin/sh
case $1/$2 in
  pre/*)
    systemctl stop NetworkManager.service
    rmmod intel_ipts
    rmmod mei_me
    rmmod mei
    rmmod mwifiex_pcie
    rmmod mwifiex
    #echo 1 > /sys/bus/pci/devices/0000\:02\:00.0/reset
    ;;
  post/*)
    modprobe mei
    modprobe mei_me
    #echo 1 > /sys/bus/pci/devices/0000\:02\:00.0/reset
    modprobe mwifiex
    modprobe mwifiex_pcie
    systemctl start NetworkManager.service
    ;;
esac

