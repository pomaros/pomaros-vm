#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Carry original component notices with compiled guest/EFI artifacts."""
import argparse
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True, help='matching common AROS source')
p.add_argument('--output', type=Path, required=True, help='new license-material directory')
p.add_argument('--build', type=Path, help='actual guest build for external component notices')
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
notices=Path(__file__).resolve().parents[1]/'distribution/notices'
for relative in ('Unicode/UNICODE-LICENSE-V3.txt', 'LLVM/LLVM-COMPILER-RT-LICENSE.txt', 'LLVM/NOTICE.md', 'Components/NOTICE.md'):
    target=a.output.parent/relative
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(notices/relative,target)
if a.build:
    ports=a.build/'bin/apple-aarch64/Ports'
    mandatory=[]
    for pattern in ('freetype2/*/docs/FTL.TXT', 'jpeg/*/README'):
        matches=list(ports.glob(pattern))
        if len(matches)!=1:p.error('one exact actual FreeType/JPEG source notice is required: '+pattern)
        mandatory.extend(matches)
    # Only top-level component notices, not entire implementation/source trees.
    originals=set(mandatory)
    for component in ports.iterdir():
        if not component.is_dir():continue
        for unpacked in component.iterdir():
            if not unpacked.is_dir():continue
            originals.update(path for path in unpacked.iterdir() if path.is_file()
                and path.name.upper().startswith(('LICENSE','COPYING','COPYRIGHT','NOTICE','README')))
    for original in sorted(originals):
        if original.is_symlink():p.error('component notice must not escape its actual source tree')
        target=a.output.parent/'Components'/original.relative_to(ports)
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(original,target)
print('package-notices PASS files=' + str(len(paths) + 1))
