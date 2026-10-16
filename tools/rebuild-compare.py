#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Rebuild current VM desktop/integration products twice and compare all products."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import stat
import shutil
import subprocess
import time

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,default=root.parent/'aros-core')
p.add_argument('--output',type=Path,required=True,help='new directory for frozen source, builds, logs and verdicts')
p.add_argument('--jobs',type=int,default=4,help='jobs per build; two builds run concurrently')
a=p.parse_args();a.source=a.source.resolve();a.output=a.output.resolve()
if a.output.exists() or not 1<=a.jobs<=32:p.error('output must be new; jobs must be 1..32')
def git(*args):return subprocess.check_output(['git','-C',str(a.source),*args],text=True).strip()
if git('status','--porcelain'):p.error('source must be clean')
lock=dict(line.split('=',1) for line in (root/'CORE.lock').read_text().splitlines() if line and not line.startswith('#'))
if git('rev-parse','HEAD')!=lock['CORE_REVISION']:p.error('requires the exact CORE.lock source revision')
revision=git('rev-parse','HEAD');epoch=git('show','-s','--format=%ct','HEAD')
origin=git('remote','get-url','origin')
a.output.mkdir(parents=True)
source=a.output/'source'
configure=a.output/'configure-core.py'
shutil.copyfile(root/'tools/configure-core.py',configure)
targets=['kernel-hidd-virtiogfx','apple-wanderer-payloads','kernel-hidd-vmpci',
         'kernel-hidd-bus','kernel-hidd-storage','kernel-virtio','kernel-usb-poseidon','kernel-usb-pcixhci','kernel-usb-classes-hub',
         'kernel-usb-classes-hubss','kernel-usb-classes-hid','workbench-c',
         'workbench-devs-networks-virtionet','workbench-network-stacks-arostcp',
         'network-dhcp-client','workbench-devs-vmbridge','workbench-devs-clipboard',
         'workbench-tools-vmexchange','workbench-tools-vmsync','workbench-tools-editor','workbench-c-cpuinfo',
         'workbench-libs-rexxsyslib','workbench-libs-commodities','kernel-keymap']
manifest={'result':'RUNNING','source_revision':revision,'source_date_epoch':epoch,
          'targets':targets,'source':str(source),'builds':{},
          'vm_tools_revision':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
          'configure_sha256':hashlib.sha256(configure.read_bytes()).hexdigest()}
def save():
    (a.output/'verdict.json').write_text(json.dumps(manifest,indent=2)+'\n')
env=dict(os.environ,SOURCE_DATE_EPOCH=epoch,TZ='UTC',LC_ALL='C')
save()

def build_one(label):
    build=a.output/('build-'+label);steps=[];started=time.monotonic()
    # One ordinary target establishes the toolchain, feature configuration and
    # generated prerequisites. Then retain one MetaMake graph for the rest:
    # its main loop executes targets sequentially and shares dependency state.
    # Going through CALL also supplies the mandatory host/target environment.
    batch=a.output/('batch-'+label+'.mk')
    batch.write_text('.PHONY: vm-comparison-batch\nvm-comparison-batch:\n\t@+$(CALL) $(MMAKE) $(MMAKE_OPTIONS) '+
                     ' '.join('AROS.'+target for target in targets[1:])+'\n')
    commands=[([targets[0]],['make','-C',str(build),'-j'+str(a.jobs),targets[0]]),
              (targets[1:],['make','-C',str(build),'-j'+str(a.jobs),'-f','Makefile',
                            '-f',str(batch),'vm-comparison-batch'])]
    logfile=a.output/('build-'+label+'.log')
    with logfile.open('w') as log:
        for group,command in commands:
            result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
            log.flush()
            # MetaMake historically returns zero for unknown targets. A large
            # SDK alone cannot turn an omitted driver into a successful build.
            unknown='Nothing known about' in logfile.read_text(errors='replace')
            code=result.returncode or (1 if unknown else 0)
            if code:steps.append({'targets':group,'returncode':code,'unknown_target':unknown})
            else:steps.extend({'target':target,'returncode':0} for target in group)
            outcome={'build':str(build),'steps':steps,'seconds':round(time.monotonic()-started,2),'returncode':code}
            (a.output/('build-'+label+'-progress.json')).write_text(json.dumps(outcome,indent=2)+'\n')
            print(f'{label} targets={len(group)} rc={code}',flush=True)
            if code:break
    return outcome

def inventory(label):
    sdk=a.output/('build-'+label)/'bin/apple-aarch64'
    required=['Devs/Drivers/virtiogfx.hidd','Devs/Drivers/vmpci.hidd',
              'Devs/virtio.device','Devs/USBHardware/pcixhci.device',
              'Classes/USB/hub.class','Classes/USB/hubss.class','Classes/USB/hid.class',
              'Devs/Networks/virtionet.device','System/Network/AROSTCP/C/AROSTCP',
              'System/Network/AROSTCP/C/dhclient','Devs/vmbridge.device','Devs/clipboard.device',
              'C/VMExchange','C/VMSync','Tools/Editor','C/CPUInfo','C/Assign','C/MakeDir','C/Mount','C/LoadResource',
              'Libs/poseidon.library','Devs/Drivers/bus.hidd','Devs/Drivers/storage.hidd','L/fat-handler',
              'Libs/rexxsyslib.library','Libs/commodities.library','Libs/keymap.library',
              'System/Wanderer/Wanderer','boot/apple/Devs/kernel.resource','Libs/dos.library']
    for name in required:
        with (sdk/'AROS'/name).open('rb') as stream:header=stream.read(20)
        if len(header)!=20 or header[:6]!=b'\x7fELF\x02\x01' or header[18:20]!=b'\xb7\x00':
            raise RuntimeError('required AArch64 ELF missing or invalid: '+name)
    products={}
    for path in sorted((sdk/'AROS').rglob('*')):
        name=path.relative_to(sdk).as_posix()
        if path.is_symlink():products[name]={'symlink':os.readlink(path)}
        elif path.is_file():
            with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
            products[name]={'sha256':digest,'mode':stat.S_IMODE(path.stat().st_mode)}
    stage=sdk/'gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-test.bin'
    products['stage2']={'sha256':hashlib.sha256(stage.read_bytes()).hexdigest()}
    if len(products)<1000:raise RuntimeError('incomplete product inventory')
    (a.output/('products-'+label+'.json')).write_text(json.dumps(products,indent=2)+'\n')
    return products

try:
    with (a.output/'clone.log').open('w') as log:
        subprocess.run(['git','clone','--local','--no-checkout',str(a.source),str(source)],stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run(['git','-C',str(source),'checkout','--detach',revision],stdout=subprocess.DEVNULL,check=True)
    frozen=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if frozen!=revision:raise RuntimeError('source changed while freezing')
    # Preserve embedded DOS repository identity, but never push from the snapshot.
    subprocess.run(['git','-C',str(source),'remote','set-url','origin',origin],check=True)
    subprocess.run(['git','-C',str(source),'config','remote.origin.pushurl','DISABLED-FROZEN-BUILD'],check=True)
    for label in ('a','b'):
        with (a.output/('configure-'+label+'.log')).open('w') as log:
            subprocess.run(['python3',str(configure),'--source',str(source),
                            '--output',str(a.output/('build-'+label)),'--source-date-epoch',epoch,
                            '--toolchain',str(root.parent/'aros-crosstools'),
                            '--core-lock',str(root/'CORE.lock')],
                           env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        print('configure '+label+' PASS',flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        pending={pool.submit(build_one,label):label for label in ('a','b')}
        for future in concurrent.futures.as_completed(pending):
            manifest['builds'][pending[future]]=future.result();save()
    if any(x['returncode'] for x in manifest['builds'].values()):raise RuntimeError('build-target-failure')
    first,second=inventory('a'),inventory('b')
    differences=[name for name in sorted(first.keys()|second.keys()) if first.get(name)!=second.get(name)]
    manifest.update(files=[len(first),len(second)],differences=differences)
    if differences:raise RuntimeError('product-mismatch')
    manifest.update(result='PASS',reason='independent-complete-target-builds-and-product-sdk-comparison')
except Exception as error:
    manifest.update(result='FAIL',reason=str(error))
finally:save()
print(json.dumps(manifest),flush=True)
raise SystemExit(manifest['result']!='PASS')
