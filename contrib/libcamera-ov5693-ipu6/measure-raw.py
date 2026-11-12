#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Measure black level and white-balance gains from OV5693 raw frames.

Capture raw frames with libcamera's `cam` (a processed stream alongside keeps AE/AWB running):

    cam -c1 -C90 -s role=viewfinder,width=1920,height=1080 -s role=raw -Fframe#.bin

Then:

    measure-raw.py dark frame-*.bin           # lens fully covered: prints the black level
    measure-raw.py wb --black 16 frame.bin    # normal scene: grey-world gains with/without clipped pixels
    measure-raw.py wb --black 16 --roi 100,20,1200,300 frame.bin   # gains from a neutral region

Frames are 2592x1944 SBGGR10 in 16-bit little-endian containers. ROI is x0,y0,x1,y1 in
half-resolution (per-channel) coordinates.
"""
import argparse
import numpy as np

W, H, CLIP = 2592, 1944, 1000   # full-resolution mode; other modes are untested (see --size)


def load(path):
    a = np.fromfile(path, dtype='<u2')
    if a.size != W * H:
        raise SystemExit(f'{path}: expected {W}x{H} unpacked SBGGR10, got {a.size} samples (use --size WxH)')
    a = a.reshape(H, W).astype(np.float64)
    # BGGR: B at (0,0), Gb at (0,1), Gr at (1,0), R at (1,1)
    return a[1::2, 1::2], a[1::2, 0::2], a[0::2, 1::2], a[0::2, 0::2]


def dark(paths):
    planes = [np.stack(c) for c in zip(*(load(p) for p in paths))]
    for name, c in zip(('R', 'Gr', 'Gb', 'B'), planes):
        print(f'{name:2}  median {np.median(c):5.1f}  mean {c.mean():6.2f}  std {c.std():5.2f}')
    # R reads high in some frames (possibly light leaking through the cover), so use G and B only.
    level = np.median(np.concatenate([c.ravel() for c in planes[1:]]))
    print(f'black level (G/B): {level:.0f} (10-bit) = {level * 64:.0f} for BlackLevel.blackLevel')


def wb(path, black, roi):
    r, gr, gb, b = load(path)
    g = (gr + gb) / 2
    clipped = (r >= CLIP) | (gr >= CLIP) | (gb >= CLIP) | (b >= CLIP)

    def gains(mask):
        mr, mg, mb = ((c[mask] - black).mean() for c in (r, g, b))
        return mg / mr, mg / mb

    everything = np.ones_like(r, dtype=bool)
    print(f'clipped quads: {100 * clipped.mean():.1f}%')
    print('grey-world, all pixels      R %.3f  B %.3f' % gains(everything))
    print('grey-world, unclipped only  R %.3f  B %.3f' % gains(~clipped))
    if roi:
        x0, y0, x1, y1 = roi
        m = np.zeros_like(r, dtype=bool)
        m[y0:y1, x0:x1] = True
        print('neutral ROI, unclipped      R %.3f  B %.3f' % gains(m & ~clipped))


def main():
    global W, H
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--size', default=f'{W}x{H}', help='raw frame size (default %(default)s; other sizes are untested)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('dark')
    d.add_argument('frames', nargs='+')
    w = sub.add_parser('wb')
    w.add_argument('frame')
    w.add_argument('--black', type=float, default=16)
    w.add_argument('--roi', type=lambda s: tuple(int(v) for v in s.split(',')))
    a = ap.parse_args()
    W, H = (int(v) for v in a.size.split('x'))
    if a.cmd == 'dark':
        dark(a.frames)
    else:
        wb(a.frame, a.black, a.roi)


if __name__ == '__main__':
    main()
