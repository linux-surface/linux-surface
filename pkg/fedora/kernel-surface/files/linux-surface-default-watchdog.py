#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path
from typing import Any


def grub2_editenv(*args: Any, **kwargs: Any) -> str:
	subprocess.run(["grub2-editenv", *args], check=True, **kwargs)


def main() -> int:
	boot: Path = Path("/boot")
	mid: Path = Path("/etc/machine-id")

	if not boot.exists():
		print("ERROR: /boot directory does not exist")
		return 1

	if not mid.exists():
		print("ERROR: /etc/machine-id does not exist")
		return 1

	blsdir: Path = boot / "loader" / "entries"

	if not blsdir.exists():
		print("ERROR: /boot/loader/entries does not exist")
		return 1

	try:
		grub2_editenv("--help", capture_output=True)
	except:
		print("ERROR: grub2-editenv is not working")
		return 1

	# Get list of surface kernels sorted by timestamp.
	#
	# We use creation time here because it represents when the kernel was installed.
	# Modification time can be a bit wonky and seems to correspond to the build date.
	kernels: list[Path] = sorted(
		boot.glob("vmlinuz-*.surface.*"),
		key=lambda x: x.stat().st_ctime,
		reverse=True,
	)

	if len(kernels) == 0:
		print("ERROR: Failed to find a surface kernel")
		return 1

	# The saved_entry property from grubenv determines what kernel is booted by default.
	# Its value is the filename of the BLS entry in /boot/loader/entries minus the file extension.
	#
	# The BLS files are named using a combination of the machine ID and the version string
	# of the kernel that is being booted. Since we have the vmlinux, we can get the version
	# from its name, and the machine ID from /etc/machine-id.
	#
	# This allows setting the default kernel without calling grubby or having to figure out
	# which path GRUB will use to boot the kernel.

	kernel: Path = kernels[0]

	machineid: str = mid.read_text().strip()
	version: str = kernel.name.lstrip("vmlinuz-")

	blscfg: Path = blsdir / "{}-{}.conf".format(machineid, version)

	# Make sure the config really exists
	if not blscfg.exists():
		print("ERROR: {} does not exist".format(blscfg))
		return 1

	print("Kernel: {}".format(kernel))
	print("BLS entry: {}".format(blscfg))

	grub2_editenv("-", "set", "saved_entry={}".format(blscfg.stem))

	# Update timestamp for rEFInd and ensure it is marked as latest across all kernels
	kernel.touch(exist_ok=True)

	return 0


if __name__ == "__main__":
	sys.exit(main())
