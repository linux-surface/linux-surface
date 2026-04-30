#!/usr/bin/env python3

import argparse
import functools
import operator
import os
import shutil
import subprocess
import time


def system(cmd: str) -> None:
    subprocess.run(cmd, shell=True, check=True)


parser = argparse.ArgumentParser(usage="Build a patched Fedora kernel")

parser.add_argument(
    "--package-name",
    help="The name of the patched package (e.g. foo -> kernel-foo).",
    required=True,
)

parser.add_argument(
    "--package-tag",
    help="The upstream tag to build.",
    required=True,
)

parser.add_argument(
    "--package-release",
    help="The release suffix of the modified package.",
    required=True,
)

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
    "--patch",
    help="Applies a patch to the kernel source.",
    action="append",
    nargs="+",
)

parser.add_argument(
    "--config",
    help="Applies a KConfig fragment to the kernel source.",
    action="append",
    nargs="+",
)

parser.add_argument(
    "--file",
    help="Copy a file into the RPM buildroot.",
    action="append",
    nargs="+",
)

parser.add_argument(
    "--buildopts",
    help="Enable or disable options of the kernel spec file.",
    action="append",
    nargs="+",
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

patches = [] if not args.patch else functools.reduce(operator.add, args.patch)
configs = [] if not args.config else functools.reduce(operator.add, args.config)
files = [] if not args.file else functools.reduce(operator.add, args.file)
buildopts = [] if not args.buildopts else functools.reduce(operator.add, args.buildopts)

# Make paths absolute.
patches = [os.path.realpath(x) for x in patches]
configs = [os.path.realpath(x) for x in configs]
files = [os.path.realpath(x) for x in files]
outdir = os.path.realpath(args.outdir)

# Clone the kernel-ark repository if it doesn't exist.
if not os.path.exists(args.ark_dir):
    system("git clone '%s' '%s'" % (args.ark_url, args.ark_dir))

os.chdir(args.ark_dir)

# Check out the requested tag.
system("git fetch --tags")
system("git clean -dfx")
system("git checkout -b 'build/%s'" % time.time())
system("git reset --hard '%s'" % args.package_tag)

# Apply patches
for patch in patches:
    system("git am -3 '%s'" % patch)

# Copy files
for file in files:
    shutil.copy(file, "redhat/fedora_files/")

# Apply config options
#
# The format that the kernel-ark tree expects is a bit different from
# a standard kernel config. Every option is split into a single file
# named after that config.
#
# Example:
#   $ cat redhat/configs/common/generic/CONFIG_PCI
#   CONFIG_PCI=y
#
# This supposedly makes things easier for Red Hat developers,
# but it also ends up being really annoying for us.
for config in configs:
    with open(config) as f:
        lines = f.readlines()

    # Filter out comments, this means only selecting lines that look like:
    #   - CONFIG_FOO=b
    #   - # CONFIG_FOO is not set
    for line in lines:
        enable = line.startswith("CONFIG_")
        disable = line.startswith("# CONFIG_")

        if not enable and not disable:
            continue

        NAME = ""

        if enable:
            NAME = line.split("=")[0]
        elif disable:
            NAME = line[2:].split(" ")[0]

        print("Applying %s" % line.rstrip("\n"))

        with open("redhat/configs/custom-overrides/generic/%s" % NAME, "w") as f:
            f.write(line)

system("git add redhat/configs/custom-overrides/generic")
system("git commit --allow-empty -m 'Merge %s config'" % args.package_name)

cmd = ["make"]

if args.mode == "rpms":
    cmd.append("dist-rpms")
else:
    cmd.append("dist-srpm")

cmd.append("SPECPACKAGE_NAME='kernel-%s'" % args.package_name)
cmd.append("DISTLOCALVERSION='.%s'" % args.package_name)
cmd.append("BUILD='%s'" % args.package_release)

if len(buildopts) > 0:
    cmd.append("BUILDOPTS='%s'" % " ".join(buildopts))

# Build RPMS
system(" ".join(cmd))

if args.mode == "rpms":
    rpmdir = "RPMS"
else:
    rpmdir = "SRPMS"

# Copy built RPMS to output directory
os.makedirs(outdir, exist_ok=True)
system("cp -r redhat/rpm/%s/* '%s'" % (rpmdir, outdir))
