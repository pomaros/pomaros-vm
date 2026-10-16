#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Verify deterministic G2 graphics or G3 Intuition framebuffer renders."""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
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


def recognize_widget(width: int, height: int, rows: list[bytes]) -> tuple[int, int, int]:
    """Validate the measured single-CPU monitor before excluding any pixels."""
    if (width, height) != (1024, 768):
        raise ValueError('monitor widget requires the pinned single-CPU 1024x768 layout')
    left, top, bottom = width - 632, 20, 521
    # Check alert signals before static glyphs. An alert covering a label must
    # never be reported as a harmless incomplete asynchronous redraw.
    # The public UP renderer has no native keyboard-backlight controls.
    # Its former KBD label row must be background, not an arbitrary glyph mask.
    # coldstart_proof.c retains CORES/CPU at the same positions and leaves this
    # row undrawn; keep it strict even when an alert overlaps the old label.
    blank_rows = (*range(183, 197), *range(355, bottom - 4))
    for y in blank_rows:
        if any(pixel(rows, x, y) != WANDERER_DESKTOP for x in range(left + 4, width - 4)):
            raise ValueError('monitor widget blank-row contract differs')
    for y in range(top + 20, bottom - 4):
        if any(pixel(rows, x, y) not in (WANDERER_DESKTOP, (0, 0, 0))
               for x in range(left + 4, width - 4)):
            raise ValueError('monitor widget body palette differs')
    for y0 in range(top+20,bottom-20,8):
        for x0 in range(left+4,width-20,8):
            if all(pixel(rows,x,y)==(0,0,0) for y in range(y0,y0+16) for x in range(x0,x0+16)):
                raise ValueError('monitor widget solid alert geometry present')
    # These black-pixel masks were measured from the actual guest renderer.
    # They identify the retained static CORES and CPU labels, without OCR or a broad
    # exception for arbitrary grey windows. Dynamic clock/load text is separate.
    labels = [(315, 72, 'd76c6c31f2c8225f6228625a11104f97e9a04e02fe99e12cd0a5670ee4c676af'),
              (337, 48, '1a34af2dbb6f8ccfc995db88c9fe0dcafb59b11c241226c2657e8ebe653a8bca')]
    for y, span, expected in labels:
        mask = bytes(int(pixel(rows, x, yy) == (0, 0, 0))
                     for yy in range(y, y + 14) for x in range(left + 14, left + 14 + span))
        if hashlib.sha256(mask).hexdigest() != expected:
            raise ValueError('monitor widget static glyph contract differs')
    for y, color in ((top, (238, 238, 238)), (bottom - 4, (238, 238, 238)), (bottom - 1, (0, 0, 0))):
        if sum(pixel(rows, x, y) == color for x in range(left + 4, width - 4)) < 620:
            raise ValueError('monitor widget frame contract differs')
    return left, top, bottom


def verify_widget(width: int, height: int, rows: list[bytes]) -> str:
    left, top, bottom = recognize_widget(width, height, rows)
    stripped = [bytearray(row) for row in rows]
    for y in range(top, bottom):
        stripped[y][left*3:] = bytes(WANDERER_DESKTOP) * (width - left)
    blue = count_region(stripped, 0, width, 15, height, REQUESTER_TITLE)
    if blue > 64:
        raise ValueError('requester or CLI title outside monitor widget')
    result = verify_wanderer(width, height, stripped, (1024, 768))
    return result + ' monitor-widget=frame,static-glyphs,blank-rows'


def verify_wallpaper_widget(width: int, height: int, rows: list[bytes],
                            reference: tuple[int, int, list[bytes]]) -> str:
    if (width,height)!=(1024,768):raise ValueError('production wallpaper requires 1024x768')
    left, top, bottom = 392,20,521
    stripped = [bytearray(row) for row in rows]
    for y in range(top, bottom):
        stripped[y][left*3:] = bytes(WANDERER_DESKTOP) * (width-left)
    if count_region(stripped, 0, width, 15, height, REQUESTER_TITLE) > 64:
        raise ValueError('requester or CLI title outside monitor widget')
    black = count_region(stripped, 302, 722, 344, 419, (0, 0, 0))
    alert = count_region(stripped, 0, width, 329, 444, (0, 0, 0))
    if black > 512 or alert > 1024:
        raise ValueError('software-failure/requester geometry present')
    recognize_widget(width,height,rows)
    # Production uses its configured JPEG backdrop, not the flat RAM fixture.
    # Compare 32x32 blocks outside the exact verified monitor and icon column.
    # A two-source-pixel neighbourhood admits the two bitmap scalers' rounding;
    # it cannot admit an arbitrary flat grey/black requester over the backdrop.
    sw, sh, source = reference
    if (sw, sh) != (1600, 1200):
        raise ValueError('unexpected configured wallpaper dimensions')
    worst = 0.0; blocks = 0
    for y0 in range(20, height-31, 32):
        for x0 in range(0, width, 32):
            if not (128 <= x0 and x0+32 <= left or y0 >= 528):
                continue
            total = 0.0
            for y in range(y0, y0+32, 4):
                for x in range(x0, x0+32, 4):
                    actual = pixel(rows, x, y)
                    sx, sy = x*sw//width, y*sh//height
                    total += min(sum(abs(a-b) for a,b in zip(actual,pixel(source,xx,yy)))/3
                                 for yy in range(max(0,sy-2),min(sh,sy+3))
                                 for xx in range(max(0,sx-2),min(sw,sx+3)))
            error = total/64
            if error > 8:
                raise ValueError(f'configured wallpaper differs in block {x0},{y0}: error={error:.2f}')
            worst=max(worst,error); blocks+=1
    if blocks != 352:
        raise ValueError('wallpaper coverage contract differs')
    return f'wallpaper-blocks={blocks} max-error={worst:.3f} monitor=frame,glyphs,blank-rows no-requester'


def verify_widget_negative_controls(width: int, height: int, rows: list[bytes],
                                    verifier=verify_widget) -> None:
    # Exercise both sides of the exception on the actual submitted frame.
    left = width - 632
    for x0 in (left - 80, left + 200):
        for color in ((0, 0, 0), REQUESTER_TITLE):
            alert = [bytearray(row) for row in rows]
            for y in range(height//2 - 10, height//2 + 10):
                for x in range(x0, x0 + 40):
                    alert[y][x*3:x*3+3] = bytes(color)
            try:
                verifier(width, height, alert)
            except ValueError as error:
                expected = 'blank-row contract' if x0 > left else (
                    'requester or CLI title' if color == REQUESTER_TITLE else 'software-failure/requester geometry')
                if expected not in str(error):
                    raise ValueError('widget alert control rejected for wrong reason: ' + str(error)) from error
            else:
                raise ValueError('widget classifier accepted an inside/outside alert control')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile",
                        choices=("graphics", "intuition", "wanderer", "wanderer-virtio", "wanderer-widget", "wanderer-wallpaper"),
                        default="graphics")
    parser.add_argument("--serial-log", type=Path)
    parser.add_argument('--wallpaper', type=Path)
    parser.add_argument('--wallpaper-sha256')
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        width, height, rows = decode(args.image)
        if width < 640 or height < 480:
            raise ValueError("framebuffer is smaller than the G2 fixture")
        if args.profile in ("wanderer-widget", "wanderer-wallpaper"):
            if args.serial_log is None:
                raise ValueError('monitor widget requires the matching serial log')
            serial = args.serial_log.read_text(errors='replace')
            if not all(marker in serial for marker in ('[KI] alive window open', '[KI] desktop stable')):
                raise ValueError('monitor widget serial markers missing')
            verify_wanderer_classifier((1024, 768))
            if args.profile == 'wanderer-wallpaper':
                if args.wallpaper is None or not args.wallpaper_sha256 or hashlib.sha256(args.wallpaper.read_bytes()).hexdigest()!=args.wallpaper_sha256:
                    raise ValueError('configured wallpaper must match the immutable distribution inventory')
                with tempfile.TemporaryDirectory(prefix='pomaros-wallpaper-') as directory:
                    decoded=Path(directory)/'source.png'
                    subprocess.run(['/usr/bin/sips','-s','format','png',str(args.wallpaper),'--out',str(decoded)],check=True,capture_output=True)
                    reference=decode(decoded)
                verifier=lambda w,h,r: verify_wallpaper_widget(w,h,r,reference)
            else:
                verifier=verify_widget
            result = verifier(width, height, rows)
            verify_widget_negative_controls(width, height, rows, verifier)
            if args.profile == 'wanderer-wallpaper':
                # These controls start from the proven real positive above.
                for name,color in (('generic-requester',REQUESTER_TITLE),('software-alert',(0,0,0))):
                    control=[bytearray(row) for row in rows]
                    for y in range(350,390):
                        for x in range(280,744):control[y][x*3:x*3+3]=bytes(color)
                    try:verifier(width,height,control)
                    except ValueError:pass
                    else:raise ValueError('production classifier accepted '+name)
                # Cover the removed control row and both retained static labels.
                for label_top in (180, 312, 334):
                    for color in (REQUESTER_TITLE,(0,0,0)):
                        obscured=[bytearray(row) for row in rows]
                        for y in range(label_top,label_top+24):
                            for x in range(408,456):obscured[y][x*3:x*3+3]=bytes(color)
                        try:verifier(width,height,obscured)
                        except ValueError as error:
                            if not any(reason in str(error) for reason in ('body palette','solid alert geometry','blank-row contract')):raise ValueError('label alert misclassified as incomplete glyph') from error
                        else:raise ValueError('production classifier accepted alert over glyph')
                missing=[bytearray(row) for row in rows]
                for y in range(20,height):missing[y][:392*3]=bytes(WANDERER_DESKTOP)*392
                try:verifier(width,height,missing)
                except ValueError as error:
                    if 'configured wallpaper differs' not in str(error):raise
                else:raise ValueError('production classifier accepted missing wallpaper')
        elif args.profile in ("wanderer", "wanderer-virtio"):
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
    except (OSError, ValueError, zlib.error, struct.error, subprocess.SubprocessError) as exc:
        print(f"aros-render-frame result=FAIL reason={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
