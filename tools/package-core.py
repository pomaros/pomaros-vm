#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Package AROS from a configured VM build, without a seed bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
from types import SimpleNamespace

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--profile', choices=['headless', 'wanderer'], default='headless')
p.add_argument('--toolchain', type=Path, default=Path.home() / 'aros-crosstools')
p.add_argument('--efi', type=Path, default=root / 'build/uefi/BOOTAA64.EFI')
a = p.parse_args()
build = a.build.resolve()
sdk = build / 'bin/apple-aarch64'
output = a.output.resolve()
if output.exists():
    p.error('output must not exist')
config = (sdk / 'gen/config/target.cfg').read_bytes()
if b'AROS_VM_GUEST           := 1' not in config or b'-ffixed-x18' in config:
    p.error('build must use --enable-vm-guest')
configure_command = json.loads((build / 'vm-configure-command.json').read_text())
source = Path(configure_command[0]).parent
revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True)
if dirty:
    p.error('commit or resolve core source changes before recording a package')
stage2 = sdk / 'gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-test.bin'
modules = json.loads((root / f'boot/{a.profile}-modules.json').read_text())
components = [(name, sdk / 'AROS' / rel) for name, rel in modules.items()]
efi = a.efi.resolve()
programs = [sdk / 'AROS/System/Wanderer/Wanderer'] if a.profile == 'wanderer' else []
for path in [stage2, efi] + [path for _, path in components] + programs:
    if not path.is_file():
        p.error(f'missing build product: {path}')
output.mkdir(parents=True)
probe = output / 'ABIPROBE'
subprocess.run([str(root / 'tools/build-abi-probe'), '--sdk', str(sdk),
                '--crosstools', str(a.toolchain.resolve()), str(probe)], check=True)
ramdisk = output / 'SYS.img'
image_command = ['python3', str(root / 'tools/make-fat12-image.py'), '--abi-probe', str(probe)]
if programs:
    image_command += ['--wanderer', str(programs[0])]
subprocess.run(image_command + [str(ramdisk)], check=True)
subprocess.run(image_command + ['--verify', str(ramdisk)], check=True)
bundle = runpy.run_path(str(root / 'tools/arosbundle'))
args = SimpleNamespace(kernel=stage2, kernel_name='aros-apple-stage2',
                       kernel_memory_size=16 * 1024 * 1024, component=components,
                       ramdisk=ramdisk, ramdisk_name='SYS')
data = bundle['create_bundle'](args)
entries = bundle['verify_bundle'](data)
(output / 'AROS').mkdir()
(output / 'EFI/BOOT').mkdir(parents=True)
destination = output / 'AROS/GEN0.BND'
destination.write_bytes(data)
subprocess.run(['python3', str(root / 'tools/boot_generation.py'), 'create',
                '--bundle', str(destination), '--sequence', '1', '--slot', '0',
                '--state', 'accepted', '--output', str(output / 'AROS/BOOT0.ID')], check=True)
shutil.copyfile(efi, output / 'EFI/BOOT/BOOTAA64.EFI')
inputs = [stage2, efi, probe, ramdisk] + [path for _, path in components] + programs
manifest = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
(output / 'provenance.json').write_text(json.dumps({
    'profile': a.profile, 'build': str(build), 'seed_bundle': None,
    'source_revision': revision, 'configure_command': configure_command,
    'inputs': manifest, 'bundle_sha256': hashlib.sha256(data).hexdigest(),
}, indent=2) + '\n')
print(f'package-core result=PASS modules={len(entries)} output={output}')
