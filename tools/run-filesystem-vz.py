#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Verify real FAT files through virtio across two fresh VZ VMs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--disk', type=Path, required=True)
p.add_argument('--file-exchange', action='store_true')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
base, total = 64*1024*1024, 80*1024*1024
if not a.disk.is_file() or a.disk.stat().st_size != base:
    p.error('input must be a regular 64 MiB test boot image')
boot = a.disk.read_bytes()[:512]
sector = struct.unpack_from('<H', boot, 11)[0]
sectors = struct.unpack_from('<H', boot, 19)[0] or struct.unpack_from('<I', boot, 32)[0]
if boot[510:512] != b'\x55\xaa' or sector*sectors != base:
    p.error('FAT boot volume must end exactly at 64 MiB')
if a.output.exists(): p.error('output must not exist')
a.output.mkdir(parents=True)
a.output = a.output.resolve()
image = a.output/'prepared.img'
shutil.copyfile(a.disk, image)
volume = a.output/'volume.img'
with volume.open('xb') as f: f.truncate(total-base)
subprocess.run(['mformat','-i',str(volume),'-T','32768','-v','VMTEST','::'],check=True)
seed = a.output/'SEED.TXT'
seed.write_bytes(b'MACAROS-VM-FILESYSTEM-TEST-V1\n')
subprocess.run(['mcopy','-i',str(volume),str(seed),'::SEED.TXT'],check=True)
with image.open('ab') as f: f.write(volume.read_bytes())
expected = bytes(((i*37+11)^(i>>8))&255 for i in range(65536))
verdict = {'result': 'FAIL', 'reason': 'not-run', 'backend': 'Virtualization.framework',
           'input_sha256': hashlib.sha256(a.disk.read_bytes()).hexdigest(), 'file_exchange':a.file_exchange, 'phases': []}
try:
    for phase in ['write', 'restart']:
        out = a.output/phase
        run = subprocess.run(['python3', str(root/'tools/run-vz.py'), '--disk', str(image),
            '--output', str(out), '--require-pci-hidd', '--require-msi-frame',
            '--require-msi-irq', '--require-virtio-device', '--filesystem-phase', phase, '--timeout', '20'] + (['--require-file-commands'] if a.file_exchange else []),
            capture_output=True, text=True)
        (a.output/f'{phase}-runner.log').write_text(run.stdout+run.stderr)
        if run.returncode: raise RuntimeError(f'{phase}-guest-gate')
        image = out/'system.img'
        with image.open('rb') as f:
            f.seek(base)
            tail = f.read()
        saved = out/'volume.img'
        saved.write_bytes(tail)
        read = subprocess.run(['mtype','-i',str(saved),'::DATA/KEPT.BIN'],capture_output=True)
        if read.returncode or read.stdout != expected:
            raise RuntimeError(f'{phase}-host-file-content')
        original = subprocess.run(['mtype','-i',str(saved),'::SEED.TXT'],capture_output=True)
        if original.returncode or original.stdout != seed.read_bytes():
            raise RuntimeError(f'{phase}-host-seed-content')
        pending = subprocess.run(['mtype','-i',str(saved),'::DATA/PENDING.BIN'],capture_output=True)
        if not pending.returncode: raise RuntimeError(f'{phase}-rename-left-old-file')
        if a.file_exchange:
            for name in ['EXCHANGE.BIN']+(['EXCHANG2.BIN'] if phase=='restart' else []):
                fetched=subprocess.run(['mtype','-i',str(saved),'::'+name],capture_output=True)
                if fetched.returncode or fetched.stdout!=expected:
                    raise RuntimeError(f'{phase}-downloaded-file-content-{name}')
            listing=subprocess.run(['mdir','-b','-i',str(saved),'::'],capture_output=True,text=True,check=True)
            if '.vmexchange-' in listing.stdout.lower():
                raise RuntimeError(f'{phase}-temporary-download-leftover')
        verdict['phases'].append({'phase': phase, 'file_sha256': hashlib.sha256(read.stdout).hexdigest(),
                                  'disk_sha256': hashlib.sha256(image.read_bytes()).hexdigest()})
    verdict.update(result='PASS', reason='guest-files-host-files-fresh-vm-read')
except Exception as error:
    verdict['reason'] = str(error)
finally:
    (a.output/'verdict.json').write_text(json.dumps(verdict, indent=2)+'\n')
print(json.dumps(verdict))
raise SystemExit(0 if verdict['result']=='PASS' else 1)
