#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Expand a seed SYS image into a deterministic 16 MiB FAT16 volume."""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
from fat_metadata import EPOCH, normalize_fat16

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--seed',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--file',action='append',default=[])
a=p.parse_args()
if a.output.exists():p.error('output must not exist')
epoch=EPOCH
# macOS mtools under the C locale extracts the seed's VFAT AROS.boot as
# AROS.boot_, breaking the DOS boot signature. Keep UTF-8 names end to end.
env=dict(os.environ,TZ='UTC',LC_ALL='en_US.UTF-8')
def run(args):
    return subprocess.run(args,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE).stdout
with tempfile.TemporaryDirectory() as tmp:
    stage=Path(tmp)
    run(['mcopy','-s','-i',str(a.seed),'::*',str(stage)])
    for entry in run(['mdir','-b','-s','-i',str(a.seed),'::*']).decode('utf-8').splitlines():
        if entry.endswith('/'):continue
        if not entry.startswith('::/'):raise ValueError('unexpected seed listing: '+entry)
        extracted=stage/entry[3:]
        if not extracted.is_file() or extracted.read_bytes()!=run(['mtype','-i',str(a.seed),entry]):
            raise ValueError('seed extraction changed a filename or content: '+entry)
    for spec in a.file:
        rel,sep,src=spec.partition('=');path=PurePosixPath(rel)
        if not sep or not rel or path.is_absolute() or '..' in path.parts:
            p.error('--file requires a relative destination and source')
        target=stage/rel;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(Path(src).read_bytes())
    for path in stage.rglob('*'):os.utime(path,(epoch,epoch))
    with a.output.open('xb') as out:out.truncate(16*1024*1024)
    # The boot device requires an integral cylinder count for its DosEnvec.
    run(['mformat','-i',str(a.output),'-T','32768','-h','1','-s','32','-c','2','-v','AROSB2BOOT','-N','0xb2082026','::'])
    run(['mcopy','-m','-s','-i',str(a.output),*[str(x) for x in sorted(stage.iterdir())],'::'])
    data=normalize_fat16(a.output.read_bytes())
    a.output.write_bytes(data)
    for path in stage.rglob('*'):
        if path.is_file():
            actual=run(['mtype','-i',str(a.output),'::'+path.relative_to(stage).as_posix()])
            if actual!=path.read_bytes():raise ValueError('file roundtrip mismatch: '+str(path))
print('expand-system-image result=PASS bytes='+str(len(data))+' sha256='+hashlib.sha256(data).hexdigest())
