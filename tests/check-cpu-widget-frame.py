#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Qualify the production UP CPUWidget and its immutable desktop backdrop."""
import argparse
import hashlib
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

frame = runpy.run_path(str(Path(__file__).with_name('check-aros-render-frame.py')))
decode, pixel = frame['decode'], frame['pixel']
BOX = (792, 28, 1008, 100)
# Measured on the retained real fc028f desktop; title, CPU 0 label, frame and
# empty margins are invariant. Only load text and the gauge interior vary.
STATIC_SHA = '6beaa1f143ea1131554966354c424f121c1ca3bc942ecc7f7ef4c890f1bbea05'
TEXT = (840, 54, 992, 68)
BAR = (803, 69, 988, 75)


def inside(x, y, box):
    return box[0] <= x < box[2] and box[1] <= y < box[3]


def verify(w, h, rows, reference):
    if (w, h) != (1024, 768):
        raise ValueError('CPUWidget requires the pinned 1024x768 UP layout')
    fixed = bytearray()
    for y in range(BOX[1], BOX[3]):
        for x in range(BOX[0], BOX[2]):
            color = pixel(rows, x, y)
            if inside(x, y, TEXT):
                if color not in ((153, 153, 153), (0, 0, 0)):
                    raise ValueError('CPUWidget load text palette differs')
            elif inside(x, y, BAR):
                if color not in ((238, 238, 238), (102, 136, 187)):
                    raise ValueError('CPUWidget gauge palette differs')
            else:
                fixed.extend(color)
    if hashlib.sha256(fixed).hexdigest() != STATIC_SHA:
        raise ValueError('CPUWidget frame, CPU label or static margins differ')
    for y in range(TEXT[1], TEXT[3] - 7):
        for x in range(TEXT[0], TEXT[2] - 7):
            if all(pixel(rows, xx, yy) == (0, 0, 0)
                   for yy in range(y, y + 8) for xx in range(x, x + 8)):
                raise ValueError('solid alert covers CPUWidget text')
    gauge = [pixel(rows, x, BAR[1]) for x in range(BAR[0], BAR[2])]
    if any([pixel(rows, x, y) for x in range(BAR[0], BAR[2])] != gauge
           for y in range(BAR[1], BAR[3])):
        raise ValueError('CPUWidget gauge rows disagree')
    if any(gauge[i] == (238, 238, 238) and gauge[i + 1] == (102, 136, 187)
           for i in range(len(gauge) - 1)):
        raise ValueError('CPUWidget gauge is not a contiguous load prefix')
    blue = sum(pixel(rows, x, y) == (102, 136, 187)
               for y in range(20, h) for x in range(w) if not inside(x, y, BOX))
    if blue > 64:
        raise ValueError('requester title outside CPUWidget')
    sw, sh, source = reference
    if (sw, sh) != (1600, 1200):
        raise ValueError('unexpected immutable wallpaper dimensions')
    blocks, worst = 0, 0.
    for y0 in range(20, h - 31, 32):
        for x0 in range(128, w, 32):
            if x0 < BOX[2] and x0 + 32 > BOX[0] and y0 < BOX[3] and y0 + 32 > BOX[1]:
                continue
            total = 0.
            for y in range(y0, y0 + 32, 4):
                for x in range(x0, x0 + 32, 4):
                    actual = pixel(rows, x, y)
                    # Wanderer scales its backdrop to the root window below the
                    # 20-pixel screen bar (iconwindow.c), not the whole screen.
                    sx, sy = x * sw // w, (y - 20) * sh // (h - 20)
                    total += min(sum(abs(a-b) for a,b in zip(actual, pixel(source, xx, yy))) / 3
                                 for yy in range(max(0, sy-2), min(sh, sy+3))
                                 for xx in range(max(0, sx-2), min(sw, sx+3)))
            error = total / 64
            if error > 8:
                raise ValueError(f'immutable wallpaper differs at {x0},{y0}: {error:.2f}')
            worst = max(worst, error)
            blocks += 1
    if blocks != 620:
        raise ValueError('wallpaper coverage contract differs')
    return f'wallpaper-blocks={blocks} max-error={worst:.3f} CPUWidget=frame,label,gauge'


def negatives(w, h, rows, reference):
    for x, y in ((400, 350), (800, 30), (804, 58), (850, 54), (400, 180)):
        for color in ((0, 0, 0), (102, 136, 187)):
            damaged = [bytearray(row) for row in rows]
            for yy in range(y, y + 12):
                for xx in range(x, x + 24):
                    damaged[yy][xx*3:xx*3+3] = bytes(color)
            try:
                verify(w, h, damaged, reference)
            except ValueError:
                continue
            raise ValueError(f'accepted alert control at {x},{y}')
    damaged = [bytearray(row) for row in rows]
    for y in range(200, 400):
        damaged[y][160*3:400*3] = bytes((153, 153, 153)) * 240
    try:
        verify(w, h, damaged, reference)
    except ValueError:
        return
    raise ValueError('accepted missing wallpaper control')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial-log', type=Path, required=True)
    p.add_argument('--wallpaper', type=Path, required=True)
    p.add_argument('--wallpaper-sha256', required=True)
    p.add_argument('--pixels-only', action='store_true', help='offline image-oracle controls; no runtime acceptance')
    p.add_argument('image', type=Path)
    a = p.parse_args()
    try:
        serial = a.serial_log.read_text(errors='replace')
        if not a.pixels_only and len(re.findall(r'^\[VM\] CPU widget ready cores=1 online=1 samples=1\r?$', serial, re.M)) != 1:
            raise ValueError('missing unique live UP CPU sample readiness')
        if '[KI] desktop accepted' not in serial or '[KI] desktop stable' not in serial:
            raise ValueError('desktop readiness missing')
        if hashlib.sha256(a.wallpaper.read_bytes()).hexdigest() != a.wallpaper_sha256:
            raise ValueError('wallpaper differs from immutable package inventory')
        with tempfile.TemporaryDirectory(prefix='cpu-widget-wallpaper-') as directory:
            source = Path(directory) / 'wallpaper.png'
            subprocess.run(['/usr/bin/sips', '-s', 'format', 'png', str(a.wallpaper), '--out', str(source)], check=True, capture_output=True)
            reference = decode(source)
        w, h, rows = decode(a.image)
        result = verify(w, h, rows, reference)
        negatives(w, h, rows, reference)
        print(f'cpu-widget-frame result=PASS scope={"offline-pixels" if a.pixels_only else "live-UP-desktop"} {result} negative-controls=11')
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('cpu-widget-frame result=FAIL reason=' + str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
