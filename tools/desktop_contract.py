# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Check VM bundle coverage against the core's authoritative resident table."""
from pathlib import Path
import re


def cpu_widget_ready_matches(text, cpus):
    if not 1 <= cpus <= 64 or text.count('[VM] CPU widget ready') != 1:
        return False
    rows = re.findall(r'^\[VM\] CPU widget ready cores=(\d+) online=(\d+) samples=(\d+)\r?$', text, re.M)
    return len(rows) == 1 and tuple(map(int, rows[0])) == (cpus, cpus, cpus)


def require_guest_no_crash(text):
    """A successful host shutdown cannot hide a suspended guest task."""
    for line in text.splitlines():
        if line.startswith(('[KI] task crashed and suspended:', '[TRAP] task=',
                            '[KI] crash fault:')):
            raise ValueError("guest task crash: " + line)


def production_desktop_ready(text):
    return cpu_widget_ready_matches(text, 1) and all(text.count(marker) == 1 for marker in (
        '[KI] desktop stable', '[KI] desktop accepted', '[VM] desktop two HID bindings PASS'))


def require_nonuniform_guest_viewport(width, height, rows):
    """Refuse empty fixed-size VZ desktops, excluding the macOS title bar.

    This does not replace the QEMU pixel-exact wallpaper/widget oracle.
    """
    scale = width // 1024
    top = height - 768 * scale
    if (scale not in (1, 2, 3, 4) or width != 1024 * scale
            or not 0 <= top <= 128 * scale or len(rows) != height
            or any(len(row) != width * 3 for row in rows)):
        raise ValueError('unsupported fixed-size VZ window capture geometry')
    colors = {tuple(rows[top + y * scale][x * scale * 3:x * scale * 3 + 3])
              for y in range(64, 704, 16) for x in range(64, 960, 16)}
    if len(colors) < 16:
        raise ValueError('guest viewport is blank or lacks the production backdrop')
    return dict(backing_scale=scale, titlebar_pixel_rows=top,
                distinct_sampled_guest_colors=len(colors))


def require_desktop_residents(source, modules, virtio_gpu=False):
    path = Path(source) / 'arch/aarch64-apple/kernel/coldstart_proof.c'
    text = path.read_text()
    table = re.search(r'static const struct ColdStartResident desktop_residents\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if table is None:
        raise ValueError('cannot locate the core desktop resident contract')
    names = re.findall(r'\{\s*"([^"\n]+)"\s*,', table.group(1))
    if not names or len(names) != len(set(names)):
        raise ValueError('empty or duplicate core desktop resident contract')
    required = set(names)
    if virtio_gpu:
        required.discard('efifbgfx.hidd')
        required.add('virtiogfx.hidd')
    missing = required.difference(modules)
    if missing:
        raise ValueError('desktop bundle lacks common residents: ' + ', '.join(sorted(missing)))
