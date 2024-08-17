#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path

#####################################################################

##
## The name of the modified kernel package.
##
PACKAGE_NAME = "surface"

##
## https://gitlab.com/cki-project/kernel-ark/-/tags
##
## Fedora tags: kernel-X.Y.Z
## Upstream tags: vX.Y.Z
##
PACKAGE_TAG = "kernel-6.10.5-0"

##
## The release number of the modified kernel package.
## e.g. 300 for kernel-6.3.1-300.fc38.foo
##
PACKAGE_RELEASE = "1"

##
## Build options for configuring which parts of the kernel package are enabled.
##
## We disable all userspace components because we only want the kernel + modules.
## We also don't care too much about debug info or UKI.
##
## To list the available options, run make dist-full-help in the kernel-ark tree.
##
KERNEL_BUILDOPTS = "+up +baseonly -debuginfo -doc -headers -efiuki"

#####################################################################

parser = argparse.ArgumentParser(usage="Build a Fedora kernel with linux-surface patches")

parser.add_argument(
    "--ark-dir",
    help="The local path to the kernel-ark repository.",
    default="kernel-ark",
)

parser.add_argument(
    "--ark-url",
    help="The remote path to the kernel-ark repository.",
    default="https://gitlab.com/cki-project/kernel-ark",
)

parser.add_argument(
    "--mode",
    help="Whether to build a source RPM or binary RPMs.",
    choices=["rpms", "srpm"],
    default="rpms",
)

parser.add_argument(
    "--outdir",
    help="The directory where the built RPM files will be saved.",
    default="out",
)

args = parser.parse_args()

# The directory where this script is saved.
script = Path(sys.argv[0]).resolve().parent

# The root of the linux-surface repository.
linux_surface = script / ".." / ".." / ".."

# Determine the major version of the kernel.
kernel_version = PACKAGE_TAG.split("-")[1]
kernel_major = ".".join(kernel_version.split(".")[:2])

# Determine the patches directory and config file.
patches = linux_surface / "patches" / kernel_major
config = linux_surface / "configs" / ("surface-%s.config" % kernel_major)

sb_cert = script / "secureboot" / "MOK.crt"
sb_key = script / "secureboot" / "MOK.key"

# Check if the major version is supported.
if not patches.exists() or not config.exists():
    print("ERROR: Could not find patches / configs for kernel %s!" % kernel_major)
    sys.exit(1)

# Check if Secure Boot keys are available.
sb_avail = sb_cert.exists() and sb_key.exists()

# If we are building without secureboot, require user input to continue.
if not sb_avail:
    print("")
    print("Secure Boot keys were not configured! Using Red Hat testkeys.")
    print("The compiled kernel will not boot with Secure Boot enabled!")
    print("")

    input("Press enter to continue: ")

# Expand globs
surface_patches = sorted(patches.glob("*.patch"))

cmd = [script / "build-ark.py"]
cmd += ["--ark-dir", args.ark_dir]
cmd += ["--ark-url", args.ark_url]
cmd += ["--mode", args.mode]
cmd += ["--outdir", args.outdir]
cmd += ["--package-name", PACKAGE_NAME]
cmd += ["--package-tag", PACKAGE_TAG]
cmd += ["--package-release", PACKAGE_RELEASE]
cmd += ["--patch"] + surface_patches
cmd += ["--config", config]
cmd += ["--buildopts", KERNEL_BUILDOPTS]

local_patches = sorted((script / "patches").glob("*.patch"))
local_configs = sorted((script / "configs").glob("*.config"))
local_files = sorted((script / "files").glob("*"))

if len(local_patches) > 0:
    cmd += ["--patch"] + local_patches

if len(local_configs) > 0:
    cmd += ["--config"] + local_configs

if len(local_files) > 0:
    cmd += ["--file"] + local_files

if sb_avail:
    sb_patches = sorted((script / "secureboot").glob("*.patch"))
    sb_configs = sorted((script / "secureboot").glob("*.config"))

    if len(sb_patches) > 0:
        cmd += ["--patch"] + sb_patches

    if len(sb_configs) > 0:
        cmd += ["--config"] + sb_configs

    cmd += ["--file", sb_cert, sb_key]

subprocess.run(cmd, check=True)
