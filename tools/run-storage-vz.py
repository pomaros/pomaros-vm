#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Verify isolated block writes/flush and persistence across two fresh VZ VMs."""
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
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
base, total = 64*1024*1024, 68*1024*1024
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
magic = b'POMAROS-VM-STORAGE-TEST-V1\n'.ljust(4096, b'\0')
pattern = bytes(((i*37+11)^(i>>8))&255 for i in range(4096))
with image.open('r+b') as f:
    f.truncate(total)
    f.seek(base)
    f.write(magic)
expected = magic + pattern + bytes(total-base-8192)
verdict = {'result': 'FAIL', 'reason': 'not-run', 'backend': 'Virtualization.framework',
           'input_sha256': hashlib.sha256(a.disk.read_bytes()).hexdigest(), 'phases': []}
try:
    for phase in ['write', 'restart']:
        out = a.output/phase
        run = subprocess.run(['python3', str(root/'tools/run-vz.py'), '--disk', str(image),
            '--output', str(out), '--require-pci-hidd', '--require-msi-frame',
            '--require-msi-irq', '--require-virtio-device', '--storage-phase', phase, '--timeout', '20'],
            capture_output=True, text=True)
        (a.output/f'{phase}-runner.log').write_text(run.stdout+run.stderr)
        if run.returncode: raise RuntimeError(f'{phase}-guest-gate')
        image = out/'system.img'
        with image.open('rb') as f:
            f.seek(base)
            tail = f.read()
        if tail != expected: raise RuntimeError(f'{phase}-host-content-or-guard-mismatch')
        verdict['phases'].append({'phase': phase, 'tail_sha256': hashlib.sha256(tail).hexdigest(),
                                  'disk_sha256': hashlib.sha256(image.read_bytes()).hexdigest()})
    verdict.update(result='PASS', reason='guest-flush-host-bytes-fresh-vm-read')
except Exception as error:
    verdict['reason'] = str(error)
finally:
    (a.output/'verdict.json').write_text(json.dumps(verdict, indent=2)+'\n')
print(json.dumps(verdict))
raise SystemExit(0 if verdict['result']=='PASS' else 1)
