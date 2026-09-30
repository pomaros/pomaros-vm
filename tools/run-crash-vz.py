#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Kill a VZ write workload without guest shutdown and verify durable files."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--disk',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if a.output.exists():p.error('output must not exist')
a.output.mkdir(parents=True);a.output=a.output.resolve()
verdict={'result':'FAIL','reason':'not-run','backend':'Virtualization.framework'}
try:
    baseline=subprocess.run(['python3',str(root/'tools/run-filesystem-vz.py'),
        '--disk',str(a.disk.resolve()),'--output',str(a.output/'baseline')],capture_output=True,text=True)
    (a.output/'baseline-runner.log').write_text(baseline.stdout+baseline.stderr)
    if baseline.returncode:raise RuntimeError('baseline-failed')
    verdict['input_sha256']=hashlib.sha256(a.disk.read_bytes()).hexdigest()
    verdict['launcher_sha256']=hashlib.sha256((root/'build/macaros-vm').read_bytes()).hexdigest()
    source=a.output/'baseline/restart/system.img'
    crash=a.output/'crash.img';shutil.copyfile(source,crash)
    # Geometry selects crash mode, outside the mounted 64..80 MiB volume.
    with crash.open('r+b') as f:f.truncate(84*1024*1024)
    log=a.output/'crash-console.log'
    armed=False;start=time.monotonic()
    with log.open('w') as output:
        proc=subprocess.Popen([str(root/'build/macaros-vm'),str(crash),str(a.output/'crash-state'),
                               '--headless'],stdout=output,stderr=subprocess.STDOUT)
        try:
            while time.monotonic()-start<20:
                text=log.read_text(errors='replace')
                if 'FAIL:' in text or 'unexpectedly finished' in text:raise RuntimeError('workload-failed')
                if proc.poll() is not None:raise RuntimeError('workload-exited')
                required=['[VM] full virtio.device unit open PASS','[VM] filesystem mount PASS',
                          '[VM] filesystem content PASS','[VM] crash write workload active']
                if all(text.count(marker)==1 for marker in required):
                    proc.kill();proc.wait(timeout=5);armed=True;break
                time.sleep(.01)
        finally:
            if proc.poll() is None:proc.kill();proc.wait(timeout=5)
    verdict['kill_returncode']=proc.returncode
    verdict['workload_observed']=armed
    if not armed or proc.returncode!=-9:raise RuntimeError('controlled-kill-not-proven')
    # Preserve exact post-kill image. Recovery removes only unused mode trailer;
    # no filesystem byte is rewritten or repaired before guest remount.
    recovery=a.output/'recovery-input.img';shutil.copyfile(crash,recovery)
    with recovery.open('r+b') as f:f.truncate(80*1024*1024)
    with recovery.open('rb') as f:f.seek(64*1024*1024);tail=f.read()
    volume=a.output/'post-kill-volume.img';volume.write_bytes(tail)
    verdict['post_kill_volume_sha256']=hashlib.sha256(tail).hexdigest()
    fsck=subprocess.run(['/sbin/fsck_msdos','-n',str(volume)],capture_output=True,text=True,timeout=20)
    (a.output/'fsck-readonly.log').write_text(fsck.stdout+fsck.stderr)
    verdict['readonly_fsck_returncode']=fsck.returncode
    run=subprocess.run(['python3',str(root/'tools/run-vz.py'),'--disk',str(recovery),
        '--output',str(a.output/'recovery'),'--require-pci-hidd','--require-msi-frame',
        '--require-msi-irq','--require-virtio-device','--filesystem-phase','restart','--timeout','20'],
        capture_output=True,text=True)
    (a.output/'recovery-runner.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError('recovery-guest-failed')
    expected=bytes(((i*37+11)^(i>>8))&255 for i in range(65536))
    for name,image in [('post-kill',recovery),('post-recovery',a.output/'recovery/system.img')]:
        read=subprocess.run(['mtype','-i',str(image)+'@@67108864','::DATA/KEPT.BIN'],capture_output=True)
        if read.returncode or read.stdout!=expected:raise RuntimeError(name+'-durable-content-failed')
    verdict['durable_file_sha256']=hashlib.sha256(expected).hexdigest()
    verdict['remount_and_durable_file']='PASS'
    if fsck.returncode:raise RuntimeError('filesystem-inconsistency-after-kill')
    verdict.update(result='PASS',reason='abrupt-open-file-workload-remount-durable-file')
except Exception as error:
    verdict['reason']=str(error)
finally:
    (a.output/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict));raise SystemExit(0 if verdict['result']=='PASS' else 1)
