#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bound a real host-requested shutdown; require guest quiescence before exit."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--disk', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if not a.disk.is_file() or a.disk.stat().st_size != 320*1024*1024:
    p.error('requires a 320 MiB desktop WORK image with --shutdown-probe and a live session')
if a.output.exists(): p.error('output must not exist')
a.output.mkdir(parents=True)
a.output = a.output.resolve()
disk = a.output/'system.img'
shutil.copyfile(a.disk, disk)
launcher = root/'build/macaros-vm'
verdict = {'result':'FAIL', 'reason':'not-run', 'backend':'Virtualization.framework',
           'input_disk_sha256':hashlib.sha256(disk.read_bytes()).hexdigest(),
           'launcher_sha256':hashlib.sha256(launcher.read_bytes()).hexdigest()}
log = a.output/'console.log'
proc = None
start = time.monotonic()
try:
    with log.open('wb') as output:
        proc = subprocess.Popen([str(launcher),str(disk),str(a.output/'state'),
            '--headless','--gpu','--usb-input','--network','--host-services','--clipboard-test'],
            stdout=output,stderr=subprocess.STDOUT)
        while time.monotonic()-start<30:
            text = log.read_text(errors='replace')
            if 'FAIL:' in text or proc.poll() is not None: raise RuntimeError('startup-failure')
            if ('[VM] desktop WORK shutdown-write ready' in text and
                '[KI] desktop stable' in text and '[VM] desktop two HID bindings PASS' in text):
                break
            time.sleep(0.1)
        else: raise RuntimeError('startup-timeout')
        # No --force-stop. This is the same SIGTERM entry as a normal host quit.
        proc.terminate()
        try: code = proc.wait(timeout=20)
        except subprocess.TimeoutExpired: raise RuntimeError('guest-shutdown-timeout')
        if code != 0: raise RuntimeError('launcher-exit')
    text = log.read_text(errors='replace')
    markers = ['[HOST] guest shutdown requested','[VMSync] volume quiesced PASS',
               '[HOST] guest volume quiesced PASS']
    if 'FAIL:' in text or any(text.count(m)!=1 for m in markers):
        raise RuntimeError('missing-or-duplicate-shutdown-evidence')
    positions = [text.index(m) for m in markers]
    if positions != sorted(positions): raise RuntimeError('shutdown-order')
    data = subprocess.check_output(['mtype','-i',str(disk)+'@@67108864','::VM-WORK-QUALIFY.bin'])
    expected = bytes(((i*37+11)^(i>>8))&255 for i in range(65536))
    if data != expected: raise RuntimeError('host-file-bytes')
    verdict.update(result='PASS',reason='host-request-guest-quiesce-stop-host-bytes',
                   file_sha256=hashlib.sha256(data).hexdigest())
except Exception as error:
    verdict['reason'] = str(error)
finally:
    if proc is not None and proc.poll() is None:
        proc.kill();proc.wait(timeout=5)
        verdict['forced_cleanup'] = True
    verdict['seconds'] = round(time.monotonic()-start,2)
    verdict['result_disk_sha256'] = hashlib.sha256(disk.read_bytes()).hexdigest()
    (a.output/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict))
raise SystemExit(0 if verdict['result']=='PASS' else 1)
