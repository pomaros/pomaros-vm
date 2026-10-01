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
p.add_argument('--source',type=Path,default=root.parent/'aros-core-vm')
p.add_argument('--output',type=Path,required=True,help='new directory for frozen source, builds, logs and verdicts')
p.add_argument('--jobs',type=int,default=4,help='jobs per build; two builds run concurrently')
a=p.parse_args();a.source=a.source.resolve();a.output=a.output.resolve()
if a.output.exists() or not 1<=a.jobs<=32:p.error('output must be new; jobs must be 1..32')
def git(*args):return subprocess.check_output(['git','-C',str(a.source),*args],text=True).strip()
if git('status','--porcelain'):p.error('source must be clean')
if git('branch','--show-current')!='codex-macaros-vm':p.error('requires isolated VM source branch')
revision=git('rev-parse','HEAD');epoch=git('show','-s','--format=%ct','HEAD')
origin=git('remote','get-url','origin')
a.output.mkdir(parents=True)
source=a.output/'source'
configure=a.output/'configure-core.py'
shutil.copyfile(root/'tools/configure-core.py',configure)
targets=['kernel-hidd-virtiogfx','apple-wanderer-payloads','kernel-hidd-vmpci',
         'kernel-virtio','kernel-usb-pcixhci','kernel-usb-classes-hub',
         'kernel-usb-classes-hubss','kernel-usb-classes-hid','workbench-c',
         'workbench-devs-networks-virtionet','workbench-network-stacks-arostcp',
         'network-dhcp-client','workbench-devs-vmbridge','workbench-devs-clipboard',
         'workbench-tools-vmexchange','workbench-tools-vmsync',
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
    with (a.output/('build-'+label+'.log')).open('w') as log:
        for target in targets:
            result=subprocess.run(['make','-C',str(build),'-j'+str(a.jobs),target],env=env,stdout=log,stderr=subprocess.STDOUT)
            steps.append({'target':target,'returncode':result.returncode})
            outcome={'build':str(build),'steps':steps,'seconds':round(time.monotonic()-started,2),'returncode':result.returncode}
            (a.output/('build-'+label+'-progress.json')).write_text(json.dumps(outcome,indent=2)+'\n')
            print(f'{label} {target} rc={result.returncode}',flush=True)
            if result.returncode:break
    return outcome

def inventory(label):
    sdk=a.output/('build-'+label)/'bin/apple-aarch64'
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
        subprocess.run(['git','clone','--local','--branch','codex-macaros-vm',str(a.source),str(source)],stdout=log,stderr=subprocess.STDOUT,check=True)
    frozen=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if frozen!=revision:raise RuntimeError('source changed while freezing')
    # Preserve embedded DOS repository identity, but never push from the snapshot.
    subprocess.run(['git','-C',str(source),'remote','set-url','origin',origin],check=True)
    subprocess.run(['git','-C',str(source),'config','remote.origin.pushurl','DISABLED-FROZEN-BUILD'],check=True)
    for label in ('a','b'):
        with (a.output/('configure-'+label+'.log')).open('w') as log:
            subprocess.run(['python3',str(configure),'--source',str(source),
                            '--output',str(a.output/('build-'+label)),'--source-date-epoch',epoch],
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
