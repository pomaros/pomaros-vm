#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Carry original BSD-selected libfdt notices with compiled guest/EFI artifacts."""
import argparse
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True, help='matching common AROS source')
p.add_argument('--output', type=Path, required=True, help='new license-material directory')
a = p.parse_args()
fdt = a.source / 'arch/aarch64-all/boot/fdt'
vendor = fdt / 'libfdt'
if a.output.exists() or not (vendor / 'LICENSE.BSD-2-Clause').is_file():
    p.error('output must be new and matching libfdt license must exist')
# Original source/header notices accompany the unmodified license text. This
# copies only the small public vendor import, never the private kernel tree.
paths = [path for path in vendor.iterdir() if path.is_file()]
if any(path.is_symlink() or path.suffix not in ('.c', '.h') and path.name != 'LICENSE.BSD-2-Clause'
       for path in paths):
    p.error('unexpected vendor license/source input')
a.output.mkdir(parents=True)
for path in paths:
    shutil.copy2(path, a.output / path.name)
shutil.copy2(fdt / 'README.md', a.output / 'AROS-import-provenance.md')
print('package-notices PASS files=' + str(len(paths) + 1))
