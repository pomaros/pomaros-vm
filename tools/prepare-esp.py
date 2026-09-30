#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reuse a verified AROS bundle, replacing its platform bootstrap and kernel."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
root = Path(__file__).resolve().parents[1]
bundle = runpy.run_path(str(root / 'tools/arosbundle'))
p = argparse.ArgumentParser()
p.add_argument('--base', type=Path, required=True)
p.add_argument('--stage2', type=Path, required=True)
p.add_argument('--kernel', type=Path, required=True)
p.add_argument('--exclude', action='append', default=[])
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if a.output.exists():
    p.error('output must be a new directory')
data = a.base.read_bytes()
entries = bundle['verify_bundle'](data)
a.output.mkdir(parents=True)
with tempfile.TemporaryDirectory() as temp:
    args = SimpleNamespace(kernel=a.stage2, kernel_name=entries[0]['name'],
                           kernel_memory_size=entries[0]['memory_size'], component=[],
                           ramdisk=None, ramdisk_name='SYS')
    for e in entries[1:]:
        if e['name'] in a.exclude: continue
        path = Path(temp) / str(e['index'])
        path.write_bytes(data[e['offset']:e['offset']+e['size']])
        if e['name'] == 'kernel.resource': path = a.kernel
        if e['type'] == bundle['TYPE']['ramdisk']:
            args.ramdisk, args.ramdisk_name = path, e['name']
        else: args.component.append((e['name'], path))
    output = bundle['create_bundle'](args)
    result = bundle['verify_bundle'](output)
    (a.output/'AROS').mkdir()
    (a.output/'EFI/BOOT').mkdir(parents=True)
    destination = a.output/'AROS/GEN0.BND'
    destination.write_bytes(output)
    subprocess.run(['python3', str(root/'tools/boot_generation.py'), 'create',
                    '--bundle',str(destination),'--sequence','1','--slot','0',
                    '--state','accepted','--output',str(a.output/'AROS/BOOT0.ID')],check=True)
    shutil.copyfile(root/'build/uefi/BOOTAA64.EFI', a.output/'EFI/BOOT/BOOTAA64.EFI')
    manifest = {str(f): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in [a.base,a.stage2,a.kernel,root/'build/uefi/BOOTAA64.EFI']}
    (a.output.parent/(a.output.name+'-provenance.json')).write_text(json.dumps(manifest,indent=2)+'\n')
print('prepare-esp result=PASS modules='+str(len(result)))
