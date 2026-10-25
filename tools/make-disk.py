#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Create a new file-backed FAT boot disk from a prepared ESP tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import shutil
import tempfile
from fat_metadata import normalize_fat16
p=argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--work-volume', action='store_true',
               help='append a new FAT16 WORK volume after the 64 MiB boot volume')
p.add_argument('--work-file', action='append', default=[], metavar='REL=PATH',
               help='add an identified application or fixture to the isolated WORK volume')
p.add_argument('--work-size', type=int, metavar='MIB',
               help='WORK volume size in MiB, 256 to 1024 in 64 MiB steps; default 256 or the packaged work_bytes')
a=p.parse_args()
work_files={}
for spec in a.work_file:
    rel, separator, source=spec.partition('='); relative=PurePosixPath(rel)
    if (not separator or not rel or rel=='.' or relative.is_absolute() or '..' in relative.parts
            or rel!=relative.as_posix() or any(c in rel for c in ':\\\0*?<>|"')
            or rel.casefold() in {name.casefold() for name in work_files}):
        p.error('--work-file requires a unique relative destination and regular source')
    source=Path(source).resolve()
    if not source.is_file():p.error('missing WORK source: '+str(source))
    with source.open('rb') as stream: digest=hashlib.file_digest(stream,'sha256').hexdigest()
    work_files[rel]={'source':str(source),'bytes':source.stat().st_size,'sha256':digest}
if work_files:a.work_volume=True
inputs_path=Path(str(a.output)+'.work-files.json')
if work_files and inputs_path.exists():p.error('WORK input receipt already exists')
provenance = a.esp/'provenance.json'
manifest = json.loads(provenance.read_text()) if provenance.is_file() else {}
work_tree = a.esp/'WORK'
if manifest.get('distribution') and not work_tree.is_dir():
    p.error('distribution requires its WORK runtime tree')
if provenance.is_file() and json.loads(provenance.read_text()).get('work_volume', False):
    a.work_volume = True
MIB = 1024*1024
def bounded_work_size(mib):
    return isinstance(mib, int) and not isinstance(mib, bool) and 256 <= mib <= 1024 and mib % 64 == 0
packaged_bytes = None
if 'work_bytes' in manifest:
    # The consumer validates the recorded geometry as strictly as its own flag:
    # a corrupt or incompatible provenance must not create an arbitrary volume.
    recorded = manifest['work_bytes']
    if manifest.get('work_volume') is not True and recorded not in (0, None):
        p.error('provenance.json records work_bytes without work_volume')
    if manifest.get('work_volume') is True:
        if not (isinstance(recorded, int) and not isinstance(recorded, bool)
                and recorded % MIB == 0 and bounded_work_size(recorded // MIB)):
            p.error('provenance.json work_bytes must be 256 to 1024 MiB in 64 MiB steps')
        packaged_bytes = recorded
if a.work_size is not None and not bounded_work_size(a.work_size):
    p.error('--work-size takes 256 to 1024 MiB in 64 MiB steps (FAT16 bounded geometry)')
if a.work_size is not None and packaged_bytes and packaged_bytes!=a.work_size*1024*1024:
    p.error('--work-size disagrees with the packaged WORK geometry in provenance.json')
if a.work_size is not None and not a.work_volume:
    p.error('--work-size requires --work-volume')
a.output.parent.mkdir(parents=True,exist_ok=True)
boot_bytes = 64*1024*1024
work_bytes = (a.work_size*1024*1024 if a.work_size else packaged_bytes or 256*1024*1024) if a.work_volume else 0
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
        with tempfile.TemporaryDirectory(prefix='pomaros-work-') as temporary:
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
            made=set()
            for relative,item in sorted(work_files.items()):
                # No overwrite of distribution content, even with FAT case folding.
                present=subprocess.run(['mdir','-i',str(volume),'::'+relative],
                                       env=env,capture_output=True)
                if present.returncode==0:raise ValueError('WORK destination exists: '+relative)
                parents=list(PurePosixPath(relative).parents)[:-1]
                for parent in reversed(parents):
                    name=parent.as_posix()
                    if name not in made:
                        exists=subprocess.run(['mdir','-i',str(volume),'::'+name],
                                              env=env,capture_output=True)
                        if exists.returncode:run(['mmd','-i',str(volume),'::'+name])
                        made.add(name)
                run(['mcopy','-i',str(volume),item['source'],'::'+relative])
                actual=subprocess.check_output(['mtype','-i',str(volume),'::'+relative],env=env)
                if len(actual)!=item['bytes'] or hashlib.sha256(actual).hexdigest()!=item['sha256']:
                    raise ValueError('WORK input changed or readback differs: '+relative)
            volume.write_bytes(normalize_fat16(volume.read_bytes()))
            with volume.open('rb') as source, a.output.open('ab') as target:
                shutil.copyfileobj(source,target)
except BaseException:
    a.output.unlink();raise
if work_files:
    with inputs_path.open('x') as stream:
        json.dump({'work_offset':boot_bytes,'work_bytes':work_bytes,'files':work_files},stream,indent=2)
        stream.write('\n')
print(f'make-disk result=PASS bytes={boot_bytes+work_bytes} '
      f'work_offset={boot_bytes if work_bytes else 0} work_bytes={work_bytes}')
