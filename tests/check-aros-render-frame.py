#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Verify deterministic G2 graphics or G3 Intuition framebuffer renders."""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
EXPECTED = (
    (255, 32, 32),
    (32, 255, 64),
    (32, 64, 255),
    (255, 224, 32),
)
INTUITION_BACKGROUND = (12, 24, 48)
INTUITION_OUTER = (240, 112, 32)
INTUITION_INNER = (32, 208, 224)
WANDERER_DESKTOP = (153, 153, 153)
REQUESTER_TITLE = (102, 136, 187)


def paeth(left: int, above: int, upper_left: int) -> int:
    prediction = left + above - upper_left
    candidates = (left, above, upper_left)
    distances = tuple(abs(prediction - value) for value in candidates)
    return candidates[distances.index(min(distances))]


def decode(path: Path) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    if not data.startswith(PNG_MAGIC):
        raise ValueError("not a PNG")
    position = len(PNG_MAGIC)
    width = height = bit_depth = color_type = interlace = 0
    compressed = bytearray()
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, \
                interlace = struct.unpack(">IIBBBBB", payload)
            if compression != 0 or filtering != 0:
                raise ValueError("unsupported PNG compression/filter method")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError("expected non-interlaced 8-bit RGB/RGBA PNG")
    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    if len(raw) != height * (stride + 1):
        raise ValueError("unexpected decompressed PNG size")
    previous = bytearray(stride)
    rows: list[bytes] = []
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        encoded = raw[offset + 1:offset + 1 + stride]
        offset += stride + 1
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = paeth(left, above, upper_left)
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}")
            row[index] = (value + predictor) & 0xFF
        rows.append(bytes(
            component
            for pixel in range(0, stride, channels)
            for component in row[pixel:pixel + 3]
        ))
        previous = row
    return width, height, rows


def pixel(rows: list[bytes], x: int, y: int) -> tuple[int, int, int]:
    start = x * 3
    return tuple(rows[y][start:start + 3])


def verify_graphics(width: int, height: int, rows: list[bytes]) -> str:
    samples = (
        pixel(rows, width // 4, height // 4),
        pixel(rows, width * 3 // 4, height // 4),
        pixel(rows, width // 4, height * 3 // 4),
        pixel(rows, width * 3 // 4, height * 3 // 4),
    )
    if samples != EXPECTED:
        raise ValueError(f"quadrant samples differ: {samples!r}")
    counts = Counter(
        tuple(row[index:index + 3])
        for row in rows for index in range(0, len(row), 3)
    )
    total = width * height
    matched = sum(counts[color] for color in EXPECTED)
    if matched < total * 99 // 100:
        raise ValueError("G2 colors do not cover 99% of the framebuffer")
    for color in EXPECTED:
        if counts[color] < total * 24 // 100:
            raise ValueError(f"G2 color {color} is underrepresented")
    return f"matched={matched}/{total} quadrants=graphics.library-RectFill"


def verify_intuition(width: int, height: int, rows: list[bytes]) -> str:
    if width != 800 or height != 600:
        raise ValueError("G3 fixture requires the locked 800x600 framebuffer")
    samples = {
        "bottom-left-background": pixel(rows, 16, height - 16),
        "bottom-right-background": pixel(rows, width - 16, height - 16),
        "window-outer": pixel(rows, 100, 110),
        "window-inner": pixel(rows, width // 2, height // 2),
    }
    expected = {
        "bottom-left-background": INTUITION_BACKGROUND,
        "bottom-right-background": INTUITION_BACKGROUND,
        "window-outer": INTUITION_OUTER,
        "window-inner": INTUITION_INNER,
    }
    if samples != expected:
        raise ValueError(f"Intuition screen/window samples differ: {samples!r}")
    counts = Counter(
        tuple(row[index:index + 3])
        for row in rows for index in range(0, len(row), 3)
    )
    total = width * height
    matched = sum(counts[color] for color in (
        INTUITION_BACKGROUND, INTUITION_OUTER, INTUITION_INNER))
    if counts[INTUITION_BACKGROUND] < total * 30 // 100:
        raise ValueError("G3 screen background is underrepresented")
    if counts[INTUITION_OUTER] < total * 15 // 100:
        raise ValueError("G3 window outer content is underrepresented")
    if counts[INTUITION_INNER] < total * 25 // 100:
        raise ValueError("G3 window inner content is underrepresented")
    if matched < total * 75 // 100:
        raise ValueError("G3 screen/window colors do not cover 75% of the frame")
    return f"matched={matched}/{total} objects=Intuition-Screen,Window"


def count_region(rows: list[bytes], x0: int, x1: int, y0: int, y1: int,
                 color: tuple[int, int, int]) -> int:
    return sum(
        pixel(rows, x, y) == color
        for y in range(y0, y1) for x in range(x0, x1)
    )


def verify_wanderer(width: int, height: int, rows: list[bytes],
                    expected_size: tuple[int, int] = (800, 600)) -> str:
    if (width, height) != expected_size:
        raise ValueError(f"G4 fixture requires the locked {expected_size} framebuffer")
    counts = Counter(
        tuple(row[index:index + 3])
        for row in rows for index in range(0, len(row), 3)
    )
    total = width * height
    if counts[WANDERER_DESKTOP] < total * 70 // 100:
        raise ValueError("Wanderer desktop does not cover 70% of the frame")

    # The deterministic RAM profile has an empty desktop.  A DOS insert-volume
    # requester or an Exec Software Failure alert draws a high-contrast window
    # across this otherwise empty centre.  Reject that geometry explicitly so
    # a process/Screen/Window marker cannot turn a crashed desktop into a pass.
    central_blue = count_region(
        rows, width//2-210, width//2+210, height//2-40, height//2+35, REQUESTER_TITLE)
    central_black = count_region(rows, width//2-210, width//2+210, height//2-40, height//2+35, (0, 0, 0))
    alert_black = count_region(rows, 0, width, height//2-55, height//2+60, (0, 0, 0))
    # Wanderer's normal top bar has an 800-pixel black separator at y=14,
    # plus black title text and icon artwork.  Ignore that chrome and inspect
    # the body below it: the Initial CLI fills this band with several lines of
    # banner/script text, while the clean deterministic desktop contains only
    # the small boot-volume icon.
    console_body_black = count_region(
        rows, 0, width, 15, 80, (0, 0, 0))
    if central_blue > 64:
        raise ValueError(
            f"requester title geometry present ({central_blue} blue pixels)")
    if central_black > 512 or alert_black > 1024:
        raise ValueError(
            "software-failure/requester geometry present "
            f"(central_black={central_black} alert_black={alert_black})")
    if console_body_black > 1024:
        raise ValueError(
            "Initial CLI obscures the desktop "
            f"({console_body_black} body black pixels)")
    return (
        f"desktop={counts[WANDERER_DESKTOP]}/{total} "
        "objects=Wanderer-backdrop,no-requester"
    )


def verify_wanderer_classifier(expected_size: tuple[int, int] = (800, 600)) -> None:
    """Keep the clean-desktop/console threshold causal, not anecdotal."""
    width, height = expected_size
    clean = [bytearray(bytes(WANDERER_DESKTOP) * width)
             for _ in range(height)]
    clean[14][:] = bytes((0, 0, 0)) * width
    verify_wanderer(width, height, clean, expected_size)

    console = [bytearray(row) for row in clean]
    remaining = 1025
    for y in range(15, 80):
        for x in range(width):
            if remaining == 0:
                break
            start = x * 3
            console[y][start:start + 3] = bytes((0, 0, 0))
            remaining -= 1
        if remaining == 0:
            break
    try:
        verify_wanderer(width, height, console, expected_size)
    except ValueError as error:
        if "Initial CLI obscures the desktop" not in str(error):
            raise ValueError(
                f"Wanderer classifier rejected console for wrong reason: {error}") \
                from error
    else:
        raise ValueError("Wanderer classifier accepted synthetic Initial CLI")


    # Keep centred requester detection valid at both admitted resolutions.
    for color, expected_reason in [(REQUESTER_TITLE, "requester title geometry"),
                                   ((0, 0, 0), "software-failure/requester geometry")]:
        requester = [bytearray(row) for row in clean]
        for y in range(height//2-10, height//2+10):
            for x in range(width//2-20, width//2+20):
                requester[y][x*3:x*3+3] = bytes(color)
        try:
            verify_wanderer(width, height, requester, expected_size)
        except ValueError as error:
            if expected_reason not in str(error):
                raise ValueError(f"requester rejected for wrong reason: {error}") from error
        else:
            raise ValueError("Wanderer classifier accepted synthetic requester")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile",
                        choices=("graphics", "intuition", "wanderer", "wanderer-virtio"),
                        default="graphics")
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        width, height, rows = decode(args.image)
        if width < 640 or height < 480:
            raise ValueError("framebuffer is smaller than the G2 fixture")
        if args.profile in ("wanderer", "wanderer-virtio"):
            expected_size = (1024, 768) if args.profile == "wanderer-virtio" else (800, 600)
            verify_wanderer_classifier(expected_size)
            result = verify_wanderer(width, height, rows, expected_size)
        elif args.profile == "intuition":
            result = verify_intuition(width, height, rows)
        else:
            result = verify_graphics(width, height, rows)
        print(
            "aros-render-frame result=PASS "
            f"profile={args.profile} width={width} height={height} {result}"
        )
        return 0
    except (OSError, ValueError, zlib.error, struct.error) as exc:
        print(f"aros-render-frame result=FAIL reason={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
