#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Create a new file-backed FAT boot disk from a prepared ESP tree."""
import argparse
import json
from pathlib import Path
import subprocess
import shutil
import tempfile
p=argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--work-volume', action='store_true',
               help='append a new 256 MiB FAT WORK volume after the 64 MiB boot volume')
a=p.parse_args()
provenance = a.esp/'provenance.json'
if provenance.is_file() and json.loads(provenance.read_text()).get('work_volume', False):
    a.work_volume = True
a.output.parent.mkdir(parents=True,exist_ok=True)
boot_bytes = 64*1024*1024
work_bytes = 256*1024*1024 if a.work_volume else 0
with a.output.open('xb') as stream: stream.truncate(boot_bytes)
try:
    subprocess.run(['mformat','-i',str(a.output),'-T','131072','::'],check=True)
    subprocess.run(['mcopy','-s','-i',str(a.output),str(a.esp/'EFI'),str(a.esp/'AROS'),'::'],check=True)
    if a.work_volume:
        # Format an isolated file: mformat must never infer its bounds from the
        # boot disk or overwrite the already populated boot filesystem.
        with tempfile.TemporaryDirectory(prefix='macaros-work-') as temporary:
            volume = Path(temporary)/'WORK.img'
            with volume.open('xb') as stream: stream.truncate(work_bytes)
            subprocess.run(['mformat','-i',str(volume),'-T',str(work_bytes//512),
                            '-v','WORK','::'],check=True)
            with volume.open('rb') as source, a.output.open('ab') as target:
                shutil.copyfileobj(source,target)
except BaseException:
    a.output.unlink();raise
print(f'make-disk result=PASS bytes={boot_bytes+work_bytes} '
      f'work_offset={boot_bytes if work_bytes else 0} work_bytes={work_bytes}')
