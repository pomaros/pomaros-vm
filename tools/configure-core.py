#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Configure a fresh VM guest build with the existing AROS cross toolchain."""
import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--python', type=Path, help='build Python with mako and PyYAML; overrides PYTHON')
p.add_argument('--smp', action='store_true', help='isolated experimental Exec SMP build')
p.add_argument('--core-lock', type=Path, default=root / 'CORE.lock',
               help='shared source pin; use an explicit lock file for experimental builds')
p.add_argument('--source-date-epoch', default=os.environ.get('SOURCE_DATE_EPOCH'),
               help='build timestamp; defaults to the source commit timestamp')
p.add_argument('--source', type=Path, default=Path(os.environ.get('AROS_CORE_SOURCE', root.parent / 'aros-core')))
p.add_argument('--toolchain', type=Path, default=root.parent / 'aros-crosstools')
p.add_argument('--source-branch', action='append', default=[],
               help='an additional core branch accepted as the isolated VM source')
a = p.parse_args()
source = a.source.resolve()
output = a.output.resolve()
toolchain = a.toolchain.resolve()
lock = dict(line.strip().split('=', 1) for line in a.core_lock.read_text().splitlines()
            if line.strip() and not line.startswith('#'))
revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
if (revision != lock.get('CORE_REVISION') or lock.get('CORE_X18_POLICY') != 'unrestricted'
        or lock.get('CORE_TARGET') != 'apple-aarch64'):
    p.error('source or register ABI differs from CORE.lock; select the pinned source or explicit experimental lock')
if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True):
    p.error('source must be clean before configuring a pinned VM guest')

if output.exists():
    p.error('output must be a new directory; configured or native builds are never reused')
branch = subprocess.check_output(['git', '-C', str(source), 'branch', '--show-current'], text=True).strip()
if branch not in ('', lock.get('CORE_BRANCH', 'main'), 'codex-smp-integration', *a.source_branch):
    p.error('select the pinned detached source or CORE.lock branch, '
            'or name the branch with --source-branch')
if not toolchain.is_dir():
    p.error('the prebuilt AROS cross-toolchain directory is missing')
epoch=a.source_date_epoch
if epoch is None:
    epoch=subprocess.check_output(['git','-C',str(source),'show','-s','--format=%ct','HEAD'],text=True).strip()
# Validate before creating a directory or interpolating into make syntax.
subprocess.run([sys.executable,str(source/'scripts/build-date.py'),'--epoch',epoch],
               check=True,stdout=subprocess.DEVNULL)
workspace_python = root.parent / 'build-venv/bin/python3'
build_python = str(a.python) if a.python else os.environ.get('PYTHON') or (
    str(workspace_python) if workspace_python.is_file() else sys.executable)
# Keep the venv entry point: resolving its final symlink selects the base
# interpreter and loses the environment's site-packages.
build_python = str(Path(shutil.which(build_python) or build_python).expanduser().absolute())
try:
    check = subprocess.run([build_python, '-c', 'import mako, yaml'],
                           capture_output=True, text=True, timeout=15)
except (OSError, subprocess.SubprocessError) as error:
    p.error('cannot use selected build Python: ' + str(error))
if check.returncode:
    p.error('build Python requires mako and PyYAML; select --python or PYTHON: ' + build_python)
env=dict(os.environ,SOURCE_DATE_EPOCH=epoch,TZ='UTC', PYTHON=build_python)
# Catalogs are source submodules, not optional pre-generated build artifacts.
# Fetch exactly the pinned revisions required by core and desktop workloads.
subprocess.run(['git', '-C', str(source), 'submodule', 'update', '--init', '--',
                'rom/dos/catalogs', 'workbench/c/LoadResource/catalogs',
                'workbench/c/R/catalogs', 'workbench/libs/asl/catalogs',
                'workbench/libs/muimaster/catalogs', 'workbench/tools/Edit/catalogs',
                'workbench/system/Wanderer/Catalogs'], check=True)
# The core's runtime search path is lib/clang/<TARGET_LLVM_VER>/lib/aros, and
# configure defaults that version to config/llvm_def, not to the installed
# clang; name the toolchain's own version so both agree.
clang_version = subprocess.check_output([str(toolchain / 'bin' / 'clang'), '--version'],
                                        text=True).split('\n')[0]
clang_version = re.search(r'clang version (\d+\.\d+\.\d+)', clang_version).group(1)
command = [str(source / 'configure'), '--target=apple-aarch64',
           '--enable-vm-guest', '--with-toolchain=llvm', '--with-aros-toolchain=yes',
           '--with-aros-toolchain-install=' + str(toolchain),
           '--with-llvm-version=' + clang_version, '--without-x']
if a.smp:
    # The immutable source/ABI lock above is the authority; a task branch
    # name does not determine whether the pinned core supports SMP.
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
    'SOURCE_DATE_EPOCH':epoch,'TZ':'UTC','source_lock':lock,
    'source_revision':subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
},indent=2)+'\n')
config = (output / 'bin/apple-aarch64/gen/config/target.cfg').read_bytes()
if b'AROS_VM_GUEST' not in config or b'-ffixed-x18' in config:
    raise SystemExit('generated configuration violates the VM register contract')
print(f'configure-core result=PASS build={output}')
