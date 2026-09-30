#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Create a new file-backed FAT boot disk from a prepared ESP tree."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
a.output.parent.mkdir(parents=True,exist_ok=True)
with a.output.open('xb') as stream: stream.truncate(64*1024*1024)
try:
    subprocess.run(['mformat','-i',str(a.output),'-T','131072','::'],check=True)
    subprocess.run(['mcopy','-s','-i',str(a.output),str(a.esp/'EFI'),str(a.esp/'AROS'),'::'],check=True)
except BaseException:
    a.output.unlink();raise
print('make-disk result=PASS bytes=67108864')
