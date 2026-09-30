#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Qualify actual headless AROS boot under Virtualization.framework."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--require-gpu',action='store_true')
p.add_argument('--filesystem-phase',choices=['write','restart'])
p.add_argument('--storage-phase',choices=['write','restart'])
p.add_argument('--require-pci-hidd',action='store_true')
p.add_argument('--require-virtio-device',action='store_true')
p.add_argument('--require-msi-frame',action='store_true')
p.add_argument('--require-msi-irq',action='store_true')
p.add_argument('--disk',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--timeout',type=float,default=45)
a=p.parse_args()
if a.require_gpu and not a.require_pci_hidd: p.error('--require-gpu requires --require-pci-hidd')
if a.filesystem_phase and not a.require_virtio_device: p.error('--filesystem-phase requires --require-virtio-device')
if a.storage_phase and not a.require_virtio_device: p.error('--storage-phase requires --require-virtio-device')
if a.require_virtio_device and not a.require_msi_irq: p.error('--require-virtio-device requires --require-msi-irq')
if a.require_msi_irq and not a.require_msi_frame: p.error('--require-msi-irq requires --require-msi-frame')
if a.require_msi_frame and not a.require_pci_hidd: p.error('--require-msi-frame requires --require-pci-hidd')
if a.output.exists(): p.error('output must not exist')
a.output.mkdir(parents=True);a.output=a.output.resolve()
disk=a.output/'system.img';shutil.copyfile(a.disk,disk)
log=a.output/'console.log'
markers=['[VM] OpenFirmware FDT + PCI config PASS',
         '[VM] GIC + CNTV deadline ready',
         '[VM] timer.device three waits PASS',
         '[VM] IRQ handler chain PASS',
         '[KI] dispatch masks: disabled 0x00000000000000c0 plain 0x0000000000000040 PASS',
         '[B2V] runtime vectors + EL0 SVC PASS',
         '[B2D] boot + two-task context dispatch PASS',
         '[B2S] DOS takeover + SYS namespace sentinel PASS',
         '[B2A] portable ABI + emulated TLS + streams + W^X/unload PASS',
         '[B2Q] AROS startup contract PASS']
if a.require_pci_hidd:
    markers += ['[VM] PCI HIDD class + reserved console PASS',
                '[VM] PCI subsystem enumerate + remove PASS',
                '[VM] virtio common config via MapPCI PASS',
                '[VM] virtio reset acknowledged PASS',
                '[VM] PCI resident install PASS',
                '[VM] PCI resident bridge after command unload PASS',
                '[VM] virtio entropy DMA completion PASS',
                '[VM] virtio block queue sector read PASS']
if a.require_msi_frame: markers += ['[VM] MSI frame mapped read PASS']
if a.require_msi_irq: markers += ['[VM] virtio block MSI-X completion PASS']
if a.require_virtio_device: markers += ['[VM] full virtio.device unit open PASS','[VM] full virtio.device IRQ read PASS']
if a.storage_phase:
    markers += ['[VM] storage FLUSH negotiated PASS']
    markers += (['[VM] storage write flush PASS','[VM] storage write readback PASS'] if a.storage_phase=='write' else ['[VM] storage restart read PASS'])
if a.filesystem_phase:
    markers += ['[VM] filesystem mount PASS', '[VM] filesystem '+('write flush' if a.filesystem_phase=='write' else 'restart')+' PASS']
if a.require_gpu: markers += ['[VM] virtio GPU display query PASS']
passed=False;reason='timeout';start=time.monotonic()
with log.open('w') as out:
    proc=subprocess.Popen([str(root/'build/macaros-vm'),str(disk),str(a.output/'state'),'--headless']+(['--gpu'] if a.require_gpu else []),stdout=out,stderr=subprocess.STDOUT)
    try:
        while time.monotonic()-start<a.timeout:
            text=log.read_text(errors='replace')
            if 'FAIL:' in text: reason='guest-failure';break
            if all(text.count(marker)==1 for marker in markers): passed=True;reason='headless-startup-contract';break
            if proc.poll() is not None: reason='early-exit';break
            time.sleep(.1)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill();proc.wait();passed=False;reason='stop-timeout'
boot=subprocess.run(['mtype','-i',str(disk),'::BOOT.LOG'],capture_output=True)
(a.output/'firmware.log').write_bytes(boot.stdout)
verdict={'result':'PASS' if passed else 'FAIL','reason':reason,
         'backend':'Virtualization.framework','graphics':a.require_gpu,'gpu_query_required':a.require_gpu,'storage_phase':a.storage_phase,'filesystem_phase':a.filesystem_phase,
         'pci_hidd_required':a.require_pci_hidd,'msi_frame_required':a.require_msi_frame,'msi_irq_required':a.require_msi_irq,'virtio_device_required':a.require_virtio_device,
         'seconds':round(time.monotonic()-start,2),
         'input_disk_sha256':hashlib.sha256(a.disk.read_bytes()).hexdigest(),
         'launcher_sha256':hashlib.sha256((root/'build/macaros-vm').read_bytes()).hexdigest()}
(a.output/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict));raise SystemExit(0 if passed else 1)
