#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""One bounded integrated desktop workload, graceful stop and independent bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--disk',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
if not a.disk.is_file() or a.disk.stat().st_size!=320*1024*1024:
    p.error('requires isolated desktop WORK image with VMSOAK, VMNET, VMCLIP and live session')
a.output.mkdir(parents=True,exist_ok=False);a.output=a.output.resolve()
disk=a.output/'system.img';shutil.copyfile(a.disk,disk)
launcher=root/'build/pomaros-vm';log=a.output/'console.log'
result={'result':'FAIL','reason':'not-started','backend':'Virtualization.framework',
        'input_disk_sha256':hashlib.sha256(disk.read_bytes()).hexdigest(),
        'launcher_sha256':hashlib.sha256(launcher.read_bytes()).hexdigest()}
proc=None;start=time.monotonic()
def expected(cycle):
    return bytes(((i*37+cycle*17)^(i>>8))&255 for i in range(65536))
try:
    with log.open('wb') as output:
        proc=subprocess.Popen([str(launcher),str(disk),str(a.output/'state'),
            '--headless','--gpu','--usb-input','--network','--host-services','--clipboard-test'],
            stdout=output,stderr=subprocess.STDOUT)
        while time.monotonic()-start<180:
            text=log.read_text(errors='replace')
            if 'FAIL:' in text or proc.poll() is not None:raise RuntimeError('guest-failure-or-exit')
            if '[VM] desktop soak complete PASS' in text:break
            time.sleep(.2)
        else:raise RuntimeError('workload-timeout')
        required=['[VM] virtio graphics IRQ completion PASS','[KI] desktop stable',
                  '[KI] shell launched','[KI] hold endless','[VM] desktop two HID bindings PASS',
                  '[VM] desktop clipboard daemon text image no-echo PASS',
                  '[VM] desktop network autostart PASS','[VM] AROSTCP DHCP DNS lookup PASS',
                  '[VM] desktop soak complete PASS']
        gate_error=None
        if any(text.count(m)!=1 for m in required):gate_error='missing-or-duplicate-evidence'
        cycles=[int(x) for x in re.findall(r'\[VM\] desktop soak cycle=(\d+) PASS',text)]
        if cycles!=list(range(60)):gate_error='cycle-sequence'
        if time.monotonic()-start<60 or text.count('[KI] tick')<25:
            gate_error='insufficient-live-duration'
        proc.terminate()
        try:code=proc.wait(timeout=20)
        except subprocess.TimeoutExpired:raise RuntimeError('shutdown-timeout')
        if code:raise RuntimeError('shutdown-exit')
    text=log.read_text(errors='replace')
    shutdown=['[HOST] guest shutdown requested','[VMSync] volume quiesced PASS',
              '[HOST] guest volume quiesced PASS']
    if 'FAIL:' in text or any(text.count(m)!=1 for m in shutdown):raise RuntimeError('shutdown-evidence')
    if [text.index(m) for m in shutdown]!=sorted(text.index(m) for m in shutdown):
        raise RuntimeError('shutdown-order')
    files={}
    for cycle in range(60):
        path=a.output/'state/Shared'/f'soak-{cycle:02}.bin';data=path.read_bytes()
        if data!=expected(cycle):raise RuntimeError('host-upload-bytes-'+str(cycle))
        files[path.name]=hashlib.sha256(data).hexdigest()
    for name in ('SOAK.bin','SOAK-RETURN.bin'):
        data=subprocess.check_output(['mtype','-i',str(disk)+'@@67108864','::'+name])
        if data!=expected(59):raise RuntimeError('persisted-WORK-bytes-'+name)
    result['host_uploads']=files
    if gate_error:raise RuntimeError(gate_error)
    result.update(result='PASS',reason='60-io-exchange-cycles-live-desktop-graceful-stop-independent-bytes',
                  cycles=60,host_uploads=files,work_file_sha256=hashlib.sha256(expected(59)).hexdigest(),
                  memory_samples=re.findall(r'desktop soak (?:begin|complete PASS) free=(\d+) largest=(\d+)',text))
except Exception as error:result['reason']=str(error)
finally:
    if proc is not None and proc.poll() is None:
        proc.kill();proc.wait(timeout=5);result['forced_cleanup']=True
    result['seconds']=round(time.monotonic()-start,2)
    result['result_disk_sha256']=hashlib.sha256(disk.read_bytes()).hexdigest()
    (a.output/'verdict.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result));raise SystemExit(result['result']!='PASS')
