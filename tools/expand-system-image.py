#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Expand a seed SYS image into a deterministic 16 MiB FAT16 volume."""
import argparse
import datetime
import hashlib
import os
from pathlib import Path, PurePosixPath
import struct
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--seed',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--file',action='append',default=[])
a=p.parse_args()
if a.output.exists():p.error('output must not exist')
epoch=int(datetime.datetime(2026,8,29,tzinfo=datetime.timezone.utc).timestamp())
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
    # mtools can set directory creation/access times independently of mtime.
    # Normalize only those metadata fields, following bounded FAT16 chains.
    data=bytearray(a.output.read_bytes())
    sector,spc,reserved,fats,entries,total,media,spf=struct.unpack_from('<HBHBHHBH',data,11)
    root_start=(reserved+fats*spf)*sector
    root_bytes=((entries*32+sector-1)//sector)*sector
    data_start=root_start+root_bytes;cluster_bytes=spc*sector
    clusters=(len(data)-data_start)//cluster_bytes
    if sector!=512 or not 4085<=clusters<65525:raise ValueError('expected FAT16 geometry')
    fat=reserved*sector;seen=set();pending=[(root_start,root_bytes)]
    date=((2026-1980)<<9)|(8<<5)|29
    def directory(cluster):
        while cluster<0xfff8:
            if cluster<2 or cluster>=clusters+2 or cluster in seen:raise ValueError('invalid directory chain')
            seen.add(cluster);pending.append((data_start+(cluster-2)*cluster_bytes,cluster_bytes))
            cluster=struct.unpack_from('<H',data,fat+2*cluster)[0]
    while pending:
        start,length=pending.pop()
        for at in range(start,start+length,32):
            if data[at]==0:break
            if data[at]==0xe5 or data[at+11]==0x0f:continue
            if data[at+11]&0x08:
                # A volume label has no VFAT long-name chain. Some mtools
                # versions emit one; fsck_msdos correctly rejects it.
                previous=at-32
                while previous>=start and data[previous+11]==0x0f:
                    data[previous]=0xe5;previous-=32
            data[at+13]=0
            struct.pack_into('<HHH',data,at+14,0,date,date)
            struct.pack_into('<HH',data,at+22,0,date)
            if data[at+11]&0x10 and data[at]!=ord('.'):
                directory(struct.unpack_from('<H',data,at+26)[0])
    a.output.write_bytes(data)
    for path in stage.rglob('*'):
        if path.is_file():
            actual=run(['mtype','-i',str(a.output),'::'+path.relative_to(stage).as_posix()])
            if actual!=path.read_bytes():raise ValueError('file roundtrip mismatch: '+str(path))
print('expand-system-image result=PASS bytes='+str(len(data))+' sha256='+hashlib.sha256(data).hexdigest())
