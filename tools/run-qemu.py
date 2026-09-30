#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded hardware-VM boot with logs, QMP screenshot and strict verdict."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import time
p = argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--accel',choices=['hvf','tcg'],default='hvf')
p.add_argument('--profile',choices=['core','wanderer'],default='wanderer')
p.add_argument('--timeout',type=float,default=60)
p.add_argument('--require-timer',action='store_true')
p.add_argument('--require-irq-chain',action='store_true')
p.add_argument('--gic-version',choices=['2','3'],default='3')
p.add_argument('--dtb',type=Path)
p.add_argument('--firmware',type=Path,default=Path('/opt/homebrew/share/qemu/edk2-aarch64-code.fd'))
a = p.parse_args()
if a.output.exists(): p.error('output must not exist')
a.output.mkdir(parents=True)
a.output = a.output.resolve()
esp = a.output/'esp'
shutil.copytree(a.esp,esp)
serial = a.output/'serial.log'
qmp = a.output/'qmp.sock'
command = ['qemu-system-aarch64','-accel',a.accel,'-M',f'virt,acpi=off,gic-version={a.gic_version}',
           '-cpu','host' if a.accel=='hvf' else 'cortex-a72','-m','512',
           '-bios',str(a.firmware),'-drive',f'format=raw,file=fat:rw:{esp}',
           '-device','ramfb','-display','none','-no-reboot','-serial',f'file:{serial}',
           '-qmp',f'unix:{qmp},server=on,wait=off']
if a.dtb:
    shutil.copyfile(a.dtb,a.output/'input.dtb')
    command += ['-dtb',str(a.output/'input.dtb')]
(a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
passed = False
reason = 'timeout'
started=time.monotonic()
with (a.output/'host.log').open('w') as log:
    proc = subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
    try:
        while time.monotonic()-started < a.timeout:
            text = serial.read_text(errors='replace') if serial.exists() else ''
            if 'FAIL:' in text or 'Synchronous Exception' in text:
                reason='guest-failure';break
            if '[B2Q] AROS startup contract PASS' in text:
                timer_ok = (text.count('[VM] timer.device three waits PASS') == 1 and
                            text.count('[KI] dispatch masks: disabled 0x00000000000000c0 plain 0x0000000000000040 PASS') == 1)
                passed=(not a.require_timer or timer_ok) and (not a.require_irq_chain or text.count('[VM] IRQ handler chain PASS')==1);reason='startup-contract' if passed else 'irq-proof-missing';break
            if proc.poll() is not None:
                reason='early-exit';break
            time.sleep(.1)
        if not passed and proc.poll() is None:
            # Independently preserve the stopped CPU before ending a failed run.
            try:
                s=socket.socket(socket.AF_UNIX);s.settimeout(3);s.connect(str(qmp))
                with s,s.makefile('rwb') as f:
                    f.readline()
                    for name,args in [('qmp_capabilities',{}),('stop',{}),
                                      ('human-monitor-command',{'command-line':'info registers'})]:
                        f.write(json.dumps({'execute':name,'arguments':args}).encode()+b'\n');f.flush()
                        while True:
                            reply=json.loads(f.readline())
                            if 'error' in reply: raise RuntimeError(reply)
                            if 'return' in reply: break
                    (a.output/'registers.txt').write_text(str(reply['return']))
            except Exception as error:
                (a.output/'debugger-error.txt').write_text(str(error))
        if passed and a.profile == 'wanderer':
            s=socket.socket(socket.AF_UNIX);s.settimeout(3);s.connect(str(qmp))
            with s,s.makefile('rwb') as f:
                f.readline()
                def execute(name, arguments=None):
                    request={'execute':name}
                    if arguments: request['arguments']=arguments
                    f.write(json.dumps(request).encode()+b'\n');f.flush()
                    while True:
                        reply=json.loads(f.readline())
                        if 'error' in reply: raise RuntimeError(reply)
                        if 'return' in reply: return reply['return']
                execute('qmp_capabilities')
                execute('screendump',{'filename':str(a.output/'desktop.png'),'format':'png'})
                execute('quit')
            check = subprocess.run(['python3', str(Path(__file__).resolve().parents[1]/'tests/check-aros-render-frame.py'), '--profile', 'wanderer', str(a.output/'desktop.png')])
            if check.returncode: passed=False;reason='desktop-render'
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill();proc.wait()
verdict={'result':'PASS' if passed else 'FAIL','reason':reason,'backend':a.accel,
         'profile':a.profile, 'desktop_checked':a.profile == 'wanderer' and passed,
         'seconds':round(time.monotonic()-started,2),'hardware_virtualization':a.accel=='hvf',
         'gic_version':int(a.gic_version),'timer_required':a.require_timer,'irq_chain_required':a.require_irq_chain,
         'bundle_sha256':hashlib.sha256((esp/'AROS/GEN0.BND').read_bytes()).hexdigest()}
(a.output/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict))
raise SystemExit(0 if passed else 1)
