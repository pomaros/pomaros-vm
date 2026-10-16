#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Configure a fresh VM guest build with the existing AROS cross toolchain."""
import argparse
import json
import os
import sys
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--smp', action='store_true', help='isolated experimental Exec SMP build')
p.add_argument('--source-date-epoch', default=os.environ.get('SOURCE_DATE_EPOCH'),
               help='build timestamp; defaults to the source commit timestamp')
p.add_argument('--source', type=Path, default=root.parent / 'aros-core-vm')
p.add_argument('--toolchain', type=Path, default=Path.home() / 'Source/aros-project/aros-crosstools')
a = p.parse_args()
source = a.source.resolve()
output = a.output.resolve()
toolchain = a.toolchain.resolve()
if output.exists():
    p.error('output must be a new directory; configured or native builds are never reused')
branch = subprocess.check_output(['git', '-C', str(source), 'branch', '--show-current'], text=True).strip()
if branch not in ('codex-macaros-vm', 'codex-smp-integration'):
    p.error('select the isolated codex-macaros-vm or codex-smp-integration source branch')
if not toolchain.is_dir():
    p.error('the prebuilt AROS cross-toolchain directory is missing')
epoch=a.source_date_epoch
if epoch is None:
    epoch=subprocess.check_output(['git','-C',str(source),'show','-s','--format=%ct','HEAD'],text=True).strip()
# Validate before creating a directory or interpolating into make syntax.
subprocess.run([sys.executable,str(source/'scripts/build-date.py'),'--epoch',epoch],
               check=True,stdout=subprocess.DEVNULL)
env=dict(os.environ,SOURCE_DATE_EPOCH=epoch,TZ='UTC')
# Catalogs are source submodules, not optional pre-generated build artifacts.
# Fetch exactly the pinned revisions required by core and desktop workloads.
subprocess.run(['git', '-C', str(source), 'submodule', 'update', '--init', '--',
                'rom/dos/catalogs', 'workbench/c/LoadResource/catalogs',
                'workbench/c/R/catalogs', 'workbench/libs/asl/catalogs',
                'workbench/libs/muimaster/catalogs', 'workbench/tools/Edit/catalogs',
                'workbench/system/Wanderer/Catalogs'], check=True)
command = [str(source / 'configure'), '--target=apple-aarch64',
           '--enable-vm-guest', '--with-toolchain=llvm', '--with-aros-toolchain=yes',
           '--with-aros-toolchain-install=' + str(toolchain), '--without-x']
if a.smp:
    if branch != 'codex-smp-integration':
        p.error('--smp requires the isolated SMP integration branch')
    command.append('--enable-target-variant=smp')
output.mkdir(parents=True)
(output / 'vm-configure-command.json').write_text(json.dumps(command, indent=2) + '\n')
with (output / 'vm-configure.log').open('w') as log:
    result = subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT, env=env)
if result.returncode:
    raise SystemExit(f'configure failed; see {output / "vm-configure.log"}')
options=output/'bin/apple-aarch64/gen/config/make.opts'
with options.open('a') as stream:
    stream.write('\n# Reproducible default; explicit environment/make overrides remain possible.\n'
                 f'SOURCE_DATE_EPOCH ?= {epoch}\nexport SOURCE_DATE_EPOCH\nexport TZ := UTC\n')
(output/'vm-build-environment.json').write_text(json.dumps({
    'SOURCE_DATE_EPOCH':epoch,'TZ':'UTC',
    'source_revision':subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
},indent=2)+'\n')
config = (output / 'bin/apple-aarch64/gen/config/target.cfg').read_bytes()
if b'AROS_VM_GUEST' not in config or b'-ffixed-x18' in config:
    raise SystemExit('generated configuration violates the VM register contract')
print(f'configure-core result=PASS build={output}')
