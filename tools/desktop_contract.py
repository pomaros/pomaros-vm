# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Check VM bundle coverage against the core's authoritative resident table."""
from pathlib import Path
import re


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
