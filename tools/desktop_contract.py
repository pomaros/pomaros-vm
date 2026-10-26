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


def production_desktop_ready(text):
    return cpu_widget_ready_matches(text, 1) and all(text.count(marker) == 1 for marker in (
        '[KI] desktop stable', '[KI] desktop accepted', '[VM] desktop two HID bindings PASS'))


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
