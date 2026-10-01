#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Create a new file-backed FAT boot disk from a prepared ESP tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
from fat_metadata import normalize_fat16
p=argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--work-volume', action='store_true',
               help='append a new 256 MiB FAT WORK volume after the 64 MiB boot volume')
a=p.parse_args()
provenance = a.esp/'provenance.json'
manifest = json.loads(provenance.read_text()) if provenance.is_file() else {}
work_tree = a.esp/'WORK'
if manifest.get('distribution') and not work_tree.is_dir():
    p.error('distribution requires its WORK runtime tree')
if provenance.is_file() and json.loads(provenance.read_text()).get('work_volume', False):
    a.work_volume = True
a.output.parent.mkdir(parents=True,exist_ok=True)
boot_bytes = 64*1024*1024
work_bytes = 256*1024*1024 if a.work_volume else 0
env=dict(os.environ,TZ='UTC',LC_ALL='en_US.UTF-8')
def run(command):
    subprocess.run(command,env=env,check=True)

# Enumerate explicitly: recursive mcopy would inherit host directory order.
directories=[];files=[]
for name in ('EFI','AROS'):
    top=a.esp/name
    if not top.is_dir() or top.is_symlink():p.error('ESP requires real EFI and AROS directories')
    for path in [top,*top.rglob('*')]:
        if path.is_symlink():p.error('ESP symlinks are not supported')
        rel=path.relative_to(a.esp).as_posix()
        if path.is_dir():directories.append(rel)
        elif path.is_file():files.append(rel)
        else:p.error('ESP contains a nonregular file')
with a.output.open('xb') as stream: stream.truncate(boot_bytes)
try:
    run(['mformat','-i',str(a.output),'-T','131072','-N','0x4d564231','::'])
    for rel in sorted(directories,key=lambda x:(x.count('/'),x)):
        run(['mmd','-i',str(a.output),'::'+rel])
    for rel in sorted(files):
        run(['mcopy','-i',str(a.output),str(a.esp/rel),'::'+rel])
    a.output.write_bytes(normalize_fat16(a.output.read_bytes()))
    if a.work_volume:
        # Format an isolated file: mformat must never infer its bounds from the
        # boot disk or overwrite the already populated boot filesystem.
        with tempfile.TemporaryDirectory(prefix='macaros-work-') as temporary:
            volume = Path(temporary)/'WORK.img'
            with volume.open('xb') as stream: stream.truncate(work_bytes)
            run(['mformat','-i',str(volume),'-T',str(work_bytes//512),
                 '-v','WORK','-N','0x4d565731','::'])
            if manifest.get('distribution'):
                paths = sorted(work_tree.rglob('*'))
                if work_tree.is_symlink() or any(p.is_symlink() or not (p.is_file() or p.is_dir()) for p in paths):
                    p.error('distribution WORK tree must contain regular files/directories')
                expected_files={'System/'+name for name in manifest['distribution_files']}
                actual_files={p.relative_to(work_tree).as_posix() for p in paths if p.is_file()}
                if expected_files!=actual_files:
                    raise ValueError('distribution tree differs from recorded inventory')
                for directory in [p for p in paths if p.is_dir()]:
                    run(['mmd','-i',str(volume),'::'+directory.relative_to(work_tree).as_posix()])
                for directory in [work_tree]+[p for p in paths if p.is_dir()]:
                    children=[p for p in sorted(directory.iterdir()) if p.is_file()]
                    relative=directory.relative_to(work_tree).as_posix()
                    destination='::/' if relative=='.' else '::/'+relative+'/'
                    for offset in range(0,len(children),64):
                        run(['mcopy','-m','-i',str(volume),*[str(p) for p in children[offset:offset+64]],destination])
                for relative, expected in manifest['distribution_files'].items():
                    actual=subprocess.check_output(['mtype','-i',str(volume),'::System/'+relative],env=env)
                    if hashlib.sha256(actual).hexdigest()!=expected['sha256']:
                        raise ValueError('distribution file roundtrip mismatch: '+relative)
            volume.write_bytes(normalize_fat16(volume.read_bytes()))
            with volume.open('rb') as source, a.output.open('ab') as target:
                shutil.copyfileobj(source,target)
except BaseException:
    a.output.unlink();raise
print(f'make-disk result=PASS bytes={boot_bytes+work_bytes} '
      f'work_offset={boot_bytes if work_bytes else 0} work_bytes={work_bytes}')
