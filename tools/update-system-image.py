#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Create a new VM disk with updated boot files and byte-identical existing WORK."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile

BOOT=64*1024*1024
WORK=256*1024*1024
TOTAL=BOOT+WORK
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--seed',type=Path,required=True,help='new application Contents/Resources/System.img')
p.add_argument('--disk',type=Path,required=True,help='stopped VM disk; opened read-only and locked')
p.add_argument('--output',type=Path,required=True,help='new disk path, never an existing destination')
a=p.parse_args()

def open_input(path):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
    try:
        info=os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size!=TOTAL:
            raise ValueError('requires regular 320 MiB VM boot/WORK disk: '+str(path))
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        return os.fdopen(fd,'rb')
    except BaseException:
        os.close(fd);raise

def volume(stream,offset,sectors):
    stream.seek(offset);b=stream.read(512)
    u16=lambda i:int.from_bytes(b[i:i+2],'little')
    total=u16(19) or int.from_bytes(b[32:36],'little')
    cluster=b[13]
    reserved=u16(14);fats=b[16];root=u16(17);fat=u16(22)
    first=reserved+fats*fat+(root*32+511)//512
    count=(total-first)//cluster if cluster else 0
    if (b[510:512]!=b'\x55\xaa' or u16(11)!=512 or total!=sectors or
        not cluster or cluster&(cluster-1) or not reserved or fats not in (1,2) or
        not root or not fat or not 4085<=count<65525 or count+2>fat*512//2):
        raise ValueError('unsupported FAT16 volume layout')

def copy_region(source,destination,offset,length):
    source.seek(offset);digest=hashlib.sha256()
    while length:
        data=source.read(min(length,1024*1024))
        if not data:raise OSError('short source read')
        destination.write(data);digest.update(data);length-=len(data)
    return digest.hexdigest()

# Publication is no-replace and atomic. Neither source is opened for writing;
# flock also refuses a disk leased by a running VZ instance.
if a.output.exists() or a.output.is_symlink():p.error('output must not exist')
if a.seed.resolve()==a.disk.resolve():p.error('seed and existing disk must differ')
a.output.parent.mkdir(parents=True,exist_ok=True)
temporary=None
try:
    with open_input(a.seed) as seed,open_input(a.disk) as disk:
        for stream in (seed,disk):volume(stream,0,BOOT//512);volume(stream,BOOT,WORK//512)
        before=[os.fstat(s.fileno()) for s in (seed,disk)]
        fd,temporary=tempfile.mkstemp(prefix='.vm-update-',dir=a.output.parent)
        with os.fdopen(fd,'wb') as output:
            boot_hash=copy_region(seed,output,0,BOOT)
            work_hash=copy_region(disk,output,BOOT,WORK)
            output.flush();os.fsync(output.fileno())
        after=[os.fstat(s.fileno()) for s in (seed,disk)]
        if any((x.st_size,x.st_mtime_ns,x.st_ctime_ns)!=(y.st_size,y.st_mtime_ns,y.st_ctime_ns)
               for x,y in zip(before,after)):
            raise RuntimeError('source changed while copying')
        os.link(temporary,a.output)
        os.unlink(temporary);temporary=None
        parent=os.open(a.output.parent,os.O_RDONLY)
        try:os.fsync(parent)
        finally:os.close(parent)
    print(json.dumps({'result':'PASS','output':str(a.output.resolve()),'bytes':TOTAL,
                      'boot_sha256':boot_hash,'work_sha256':work_hash,
                      'source_disk_unchanged':True,'work_preserved':True}))
except (OSError,ValueError,RuntimeError) as error:
    p.exit(2,f'update-system-image: {error}\n')
finally:
    if temporary is not None:os.unlink(temporary)
