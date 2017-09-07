#!/bin/sh
set -e

if [ "$2" = "hibernate" ]; then
    case "$1" in
        pre) modprobe -r mwifiex_pcie mwifiex ;;
        post) modprobe mwifiex_pcie ;;
    esac
