#!/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Use the built-in monochrome IR camera on a first-generation Surface Laptop."""

import argparse
from pathlib import Path
import os
import re
import subprocess
import sys


def attribute(path):
    try:
        return path.read_text(encoding="ascii").strip()
    except (FileNotFoundError, PermissionError):
        return ""


def supported_model(dmi):
    expected = {
        "sys_vendor": "Microsoft Corporation",
        "product_name": "Surface Laptop",
        "product_sku": "Surface_Laptop",
    }
    return all(attribute(dmi / name) == value for name, value in expected.items())


def capture_entities(graph):
    if not re.search(r"^driver\s+ipu3-cio2\s*$", graph, re.MULTILINE):
        return
    chunks = re.split(r"^\s*- entity (\d+): ", graph, flags=re.MULTILINE)
    entities = {block.partition(" (")[0]: (num, block)
                for num, block in zip(chunks[1::2], chunks[2::2])}
    for sensor, (sensor_id, block) in entities.items():
        if not re.fullmatch(r"ov7251 [\da-fA-F]+-[\da-fA-F]+", sensor):
            continue
        link = re.search(r'-> "(ipu3-csi2 (\d+))":0 \[', block)
        if not link:
            continue
        receiver, port = link.groups()
        video = "ipu3-cio2 " + port
        if receiver in entities and video in entities:
            yield video, sensor, receiver, sensor_id, entities[receiver][0]


def media_ctl(media, *args):
    return subprocess.check_output(["media-ctl", "-d", str(media), *args],
                                   text=True, env={**os.environ, "LC_ALL": "C"},
                                   timeout=5).strip()


def discover(dmi=Path("/sys/class/dmi/id"), video_sys=Path("/sys/class/video4linux"),
             device_dir=Path("/dev")):
    if not supported_model(dmi):
        raise ValueError("Only the first-generation Microsoft Surface Laptop is supported.")
    matches = []
    for media in sorted(device_dir.glob("media[0-9]*")):
        try:
            graph = media_ctl(media, "-p")
        except subprocess.CalledProcessError:
            continue  # Another, unrelated media controller may deny access.
        for video, sensor, receiver, sensor_id, receiver_id in capture_entities(graph):
            subdev = media_ctl(media, "-e", sensor)
            hw = video_sys / Path(subdev).name / "device"
            if attribute(hw / "firmware_node" / "hid") != "INT347E":
                continue
            matches.append((media_ctl(media, "-e", video), subdev, str(media),
                            media_ctl(media, "-e", receiver), sensor_id, receiver_id))
    if len(matches) != 1:
        raise ValueError("Expected one CIO2-connected INT347E/OV7251 IR sensor; "
                         "check the media topology and your camera-device access.")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--webcam", action="store_true", help="publish an on-demand PipeWire source")
    group.add_argument("--check", action="store_true", help="read ten frames; print statistics only")
    group.add_argument("--probe", action="store_true", help="report the output rate without streaming")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--passive", action="store_true", help="use ambient light only (default)")
    source.add_argument("--illuminate", action="store_true",
                        help="require a compatible OV7251 driver and request synchronized pulses")
    args = parser.parse_args()
    device = discover()
    helper = Path(__file__).resolve().with_name("surface-ir")
    mode = "pw" if args.webcam else "check" if args.check else "rate"
    argv = [str(helper), mode, *device]
    if not args.illuminate:
        argv.append("--passive")
    os.execv(helper, argv)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(3)
