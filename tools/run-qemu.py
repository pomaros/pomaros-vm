#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded hardware-VM boot with logs, QMP screenshot and strict verdict."""
import argparse
import hashlib
import json
import runpy
from pathlib import Path
import shutil
import socket
import subprocess
import time
from cpu_probe_contract import cpu_probe_matches
p = argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--accel',choices=['hvf','tcg'],default='hvf')
p.add_argument('--console',choices=['pl011','virtio'],default='pl011')
p.add_argument('--profile',choices=['core','wanderer'],default='wanderer')
p.add_argument('--timeout',type=float,default=60)
p.add_argument('--cpus',type=int,choices=range(1,65),default=1)
p.add_argument('--require-smp-probe',action='store_true',help='bounded CPU probe protocol; captures live widget instead of checking an empty desktop')
p.add_argument('--virtio-desktop',action='store_true')
p.add_argument('--require-gpu-scanout',action='store_true')
p.add_argument('--require-gpu-irq',action='store_true')
p.add_argument('--msi-controller',choices=['auto','gicv2m','its','off'],default='auto')
p.add_argument('--require-timer',action='store_true')
p.add_argument('--require-command-startup',action='store_true')
p.add_argument('--require-platform',action='store_true')
p.add_argument('--require-pci-hidd',action='store_true')
p.add_argument('--require-msi-frame',action='store_true')
p.add_argument('--require-msi-irq',action='store_true')
p.add_argument('--require-irq-chain',action='store_true')
p.add_argument('--gic-version',choices=['2','3'],default='3')
p.add_argument('--dtb',type=Path)
p.add_argument('--firmware',type=Path,default=Path('/opt/homebrew/share/qemu/edk2-aarch64-code.fd'))
a = p.parse_args()
if a.require_smp_probe and (a.cpus<2 or a.console!='virtio' or not a.virtio_desktop):
    p.error('--require-smp-probe requires --cpus >= 2 and --console virtio --virtio-desktop')
if a.virtio_desktop and (a.profile != 'wanderer' or a.console != 'virtio'):
    p.error('--virtio-desktop requires wanderer profile and virtio console')
if a.require_gpu_irq and not a.virtio_desktop:p.error('--require-gpu-irq requires --virtio-desktop')
if a.require_gpu_scanout and (not a.require_pci_hidd or a.console!='virtio'): p.error('--require-gpu-scanout requires PCI and virtio console')
if a.require_msi_irq and not a.require_msi_frame: p.error('--require-msi-irq requires --require-msi-frame')
if a.require_msi_frame and not a.require_pci_hidd: p.error('--require-msi-frame requires --require-pci-hidd')
if a.output.exists(): p.error('output must not exist')
if a.console=='virtio' and (a.gic_version!='3' or (a.profile!='core' and not a.virtio_desktop) or a.dtb):
    p.error('virtio console qualification requires ACPI, GICv3 and a core or virtio-desktop profile')
a.output.mkdir(parents=True)
a.output = a.output.resolve()
esp = a.output/'esp'
shutil.copytree(a.esp,esp)
serial = a.output/'serial.log'
qmp = a.output/'qmp.sock'
acpi='on' if a.console=='virtio' else 'off'
firmware_serial = a.output/'firmware-serial.log' if a.console=='virtio' else serial
command = ['qemu-system-aarch64','-accel',a.accel,'-M',f'virt,acpi={acpi},gic-version={a.gic_version}'+(f',msi={a.msi_controller}' if a.msi_controller!='auto' else ''),
           '-cpu','host' if a.accel=='hvf' else 'cortex-a72','-m','512','-smp',str(a.cpus),
           '-bios',str(a.firmware),'-drive',f'format=raw,file=fat:rw:{esp}',
           '-device','ramfb','-display','none','-no-reboot','-serial',f'file:{firmware_serial}',
           '-qmp',f'unix:{qmp},server=on,wait=off']
if a.console=='virtio':
    command += ['-chardev',f'file,id=vmconsole,path={serial}',
                '-device','virtio-serial-pci,disable-legacy=on,id=vmconsole',
                '-device','virtconsole,chardev=vmconsole,bus=vmconsole.0,name=aros.console']
if a.require_pci_hidd:
    command += ['-device','virtio-rng-pci,disable-legacy=on']
if a.require_gpu_scanout or a.virtio_desktop:
    command += ['-device','virtio-gpu-pci,id=gpu,xres=1024,yres=768']
if a.dtb:
    shutil.copyfile(a.dtb,a.output/'input.dtb')
    command += ['-dtb',str(a.output/'input.dtb')]
(a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
pixels_verified=False
capture_attempted=False
widget_captured=False
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
            if a.require_gpu_scanout and not capture_attempted and '[VM] virtio GPU scanout ready' in text:
                capture_attempted=True
                try:
                    sock=socket.socket(socket.AF_UNIX);sock.settimeout(3);sock.connect(str(qmp))
                    with sock,sock.makefile('rwb') as stream:
                        stream.readline()
                        for name,args in [('qmp_capabilities',{}),('screendump',{'filename':str(a.output/'scanout.png'),'format':'png','device':'gpu','head':0})]:
                            stream.write(json.dumps({'execute':name,'arguments':args}).encode()+b'\n');stream.flush()
                            while True:
                                reply=json.loads(stream.readline())
                                if 'error' in reply:raise RuntimeError(reply)
                                if 'return' in reply:break
                    decoder=runpy.run_path(str(Path(__file__).resolve().parents[1]/'tests/check-aros-render-frame.py'))
                    w,h,rows=decoder['decode'](a.output/'scanout.png')
                    actual=[decoder['pixel'](rows,int(w*x),int(h*y)) for x,y in [(0.25,0.25),(0.75,0.25),(0.25,0.75),(0.75,0.75)]]
                    expected=[(255,0,0),(0,255,0),(0,0,255),(255,255,255)]
                    pixels_verified=all(abs(a-b)<16 for pixel,color in zip(actual,expected) for a,b in zip(pixel,color))
                    (a.output/'pixels.json').write_text(json.dumps({'actual':actual,'expected':expected,'passed':pixels_verified})+'\n')
                except Exception as error:
                    (a.output/'capture-error.txt').write_text(str(error))
                    reason='gpu-capture-failed';break
            if a.require_smp_probe and not widget_captured and text.count('[SMP] sample cpu=')>=5:
                # Capture while the bounded session still owns the widget.
                try:
                    sock=socket.socket(socket.AF_UNIX);sock.settimeout(3);sock.connect(str(qmp))
                    with sock,sock.makefile('rwb') as stream:
                        stream.readline()
                        for name,args in [('qmp_capabilities',{}),('screendump',{'filename':str(a.output/'cpu-widget.png'),'format':'png','device':'gpu','head':0})]:
                            stream.write(json.dumps({'execute':name,'arguments':args}).encode()+b'\n');stream.flush()
                            while True:
                                reply=json.loads(stream.readline())
                                if 'error' in reply:raise RuntimeError(reply)
                                if 'return' in reply:break
                    decoder=runpy.run_path(str(Path(__file__).resolve().parents[1]/'tests/check-aros-render-frame.py'))
                    w,h,rows=decoder['decode'](a.output/'cpu-widget.png')
                    if (w,h)!=(1024,768):raise ValueError('unexpected widget framebuffer dimensions')
                    widget_captured=True
                except Exception as error:
                    (a.output/'capture-error.txt').write_text(str(error))
                    reason='widget-capture-failed';break
            if '[B2Q] AROS startup contract PASS' in text:
                timer_ok = (text.count('[VM] timer.device three waits PASS') == 1 and
                            text.count('[KI] dispatch masks: disabled 0x00000000000000c0 plain 0x0000000000000040 PASS') == 1)
                passed=(not a.require_timer or timer_ok) and (not a.require_irq_chain or text.count('[VM] IRQ handler chain PASS')==1) and (not a.require_platform or text.count('[VM] OpenFirmware FDT + PCI config PASS')==1) and (not a.require_pci_hidd or (text.count('[VM] PCI HIDD class + reserved console PASS')==1 and text.count('[VM] PCI subsystem enumerate + remove PASS')==1 and text.count('[VM] virtio common config via MapPCI PASS')==1 and text.count('[VM] virtio reset acknowledged PASS')==1 and text.count('[VM] PCI resident install PASS')==1 and text.count('[VM] PCI resident bridge after command unload PASS')==1 and text.count('[VM] virtio entropy DMA completion PASS')==1 and text.count('[VM] virtio block queue sector read PASS')==1))
                passed=passed and (not a.require_msi_frame or text.count('[VM] MSI frame mapped read PASS')==1)
                passed=passed and (not a.require_msi_irq or text.count('[VM] virtio block MSI-X completion PASS')==1)
                passed=passed and (not a.require_gpu_scanout or (pixels_verified and text.count('[VM] virtio GPU scanout cleanup PASS')==1 and text.count('[VM] virtio GPU display query PASS')==1))
                passed=passed and (not a.require_gpu_irq or text.count('[VM] virtio graphics IRQ completion PASS')==1)
                passed=passed and (not a.virtio_desktop or text.count('[VM] virtio graphics driver registered')==1)
                passed=passed and (not a.require_command_startup or (text.count('[VM] command startup without service PASS')==1 and text.count('[VMExchange] main argc=')==1))
                passed=passed and (not a.require_smp_probe or (cpu_probe_matches(text) and widget_captured and text.count("[KI] alive window open")==1))
                reason='startup-contract' if passed else 'proof-missing';break
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
                    for cpu in range(a.cpus):
                        for command_line in [f'cpu {cpu}', 'info registers']:
                            f.write(json.dumps({'execute':'human-monitor-command','arguments':{'command-line':command_line}}).encode()+b'\n');f.flush()
                            while True:
                                reply=json.loads(f.readline())
                                if 'error' in reply: raise RuntimeError(reply)
                                if 'return' in reply: break
                        (a.output/f'registers-cpu-{cpu}.txt').write_text(str(reply['return']))
                    # Restore CPU 0 so these dumps match registers.txt.
                    for filename,command_line in [('selected-cpu.txt','cpu 0'),('instructions.txt','x /20i $pc'),('stack.txt','x /32gx $sp')]:
                        f.write(json.dumps({'execute':'human-monitor-command','arguments':{'command-line':command_line}}).encode()+b'\n');f.flush()
                        while True:
                            reply=json.loads(f.readline())
                            if 'error' in reply: raise RuntimeError(reply)
                            if 'return' in reply: break
                        (a.output/filename).write_text(str(reply['return']))
                    if a.profile == 'wanderer':
                        args={'filename':str(a.output/'failure-desktop.png'),'format':'png'}
                        if a.virtio_desktop: args.update(device='gpu',head=0)
                        f.write(json.dumps({'execute':'screendump','arguments':args}).encode()+b'\n');f.flush()
                        while True:
                            reply=json.loads(f.readline())
                            if 'error' in reply: raise RuntimeError(reply)
                            if 'return' in reply: break
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
                execute('screendump',{'filename':str(a.output/'desktop.png'),'format':'png', **({'device':'gpu','head':0} if a.virtio_desktop else {})})
                execute('quit')
            check = None if a.require_smp_probe else subprocess.run(['python3', str(Path(__file__).resolve().parents[1]/'tests/check-aros-render-frame.py'), '--profile', 'wanderer-virtio' if a.virtio_desktop else 'wanderer', str(a.output/'desktop.png')])
            if check is not None:
                if check.returncode: passed=False;reason='desktop-render'
                else: pixels_verified=True
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill();proc.wait()
verdict={'result':'PASS' if passed else 'FAIL','reason':reason,'backend':a.accel,
         'cpus':a.cpus,'secondary_execution_verified':a.require_smp_probe and cpu_probe_matches(text),'widget_image_captured':widget_captured,'exec_smp_verified':False,
         'command_startup_verified':a.require_command_startup and passed,
         'profile':a.profile,'virtio_desktop':a.virtio_desktop,'gpu_scanout_required':a.require_gpu_scanout,'pixels_verified':pixels_verified, 'console':a.console, 'desktop_checked':a.profile == 'wanderer' and passed and not a.require_smp_probe,
         'seconds':round(time.monotonic()-started,2),'hardware_virtualization':a.accel=='hvf',
         'gic_version':int(a.gic_version),'msi_controller':a.msi_controller,'gpu_irq_verified':passed and a.require_gpu_irq,'timer_required':a.require_timer,'platform_required':a.require_platform,'pci_hidd_required':a.require_pci_hidd,'msi_frame_required':a.require_msi_frame,'msi_irq_required':a.require_msi_irq,'irq_chain_required':a.require_irq_chain,
         'bundle_sha256':hashlib.sha256((esp/'AROS/GEN0.BND').read_bytes()).hexdigest()}
(a.output/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict))
raise SystemExit(0 if passed else 1)
