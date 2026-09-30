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
p.add_argument('--virtio-gpu', action='store_true')
p.add_argument('--pci-hidd', action='store_true')
p.add_argument('--virtio-device', action='store_true')
p.add_argument('--virtionet-device', action='store_true')
p.add_argument('--network-stack', action='store_true')
p.add_argument('--startup-sequence', type=Path)
p.add_argument('--desktop-session', help='1..3600 seconds or forever; omitted for boot proof')
p.add_argument('--input-probe',action='store_true')
p.add_argument('--usb-desktop', action='store_true')
p.add_argument('--xhci-driver', action='store_true')
p.add_argument('--xhci-enumerate', action='store_true')
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--profile', choices=['headless', 'wanderer'], default='headless')
p.add_argument('--toolchain', type=Path, default=Path.home() / 'aros-crosstools')
p.add_argument('--efi', type=Path, default=root / 'build/uefi/BOOTAA64.EFI')
a = p.parse_args()
if a.network_stack:
    a.virtionet_device=True
if a.input_probe and (not a.usb_desktop or a.desktop_session is None):
    p.error('--input-probe requires --usb-desktop and --desktop-session')
if a.desktop_session is not None:
    if a.profile != 'wanderer' or not a.virtio_gpu:
        p.error('--desktop-session requires wanderer and --virtio-gpu')
    if a.desktop_session != 'forever' and not (a.desktop_session.isascii() and a.desktop_session.isdecimal() and not a.desktop_session.startswith('0') and 1 <= int(a.desktop_session) <= 3600):
        p.error('--desktop-session must be 1..3600 or forever')
if a.startup_sequence and (a.profile != 'wanderer' or a.usb_desktop):
    p.error('--startup-sequence requires wanderer without --usb-desktop')
if a.usb_desktop and not (a.profile == 'wanderer' and a.virtio_gpu):
    p.error('--usb-desktop requires --profile wanderer --virtio-gpu')
if a.virtio_gpu and a.profile != 'wanderer':
    p.error('--virtio-gpu requires wanderer profile')
if a.pci_hidd and a.profile != 'headless':
    p.error('--pci-hidd requires headless profile')
if a.xhci_enumerate and not a.xhci_driver:
    p.error('--xhci-enumerate requires --xhci-driver')
if a.xhci_driver and not a.pci_hidd:
    p.error('--xhci-driver requires --pci-hidd')
if a.virtio_device and not a.pci_hidd:
    p.error('--virtio-device requires --pci-hidd')
if a.virtionet_device and not a.pci_hidd:
    p.error('--virtionet-device requires --pci-hidd')
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
if a.virtio_gpu:
    vm_modules = {}
    for name, rel in modules.items():
        if name == 'efifbgfx.hidd':
            name, rel = 'virtiogfx.hidd', 'Devs/Drivers/virtiogfx.hidd'
        vm_modules[name] = rel
        if name == 'rtkit.resource':
            vm_modules['openfirmware.resource'] = 'Devs/openfirmware.resource'
        if name == 'hiddclass.hidd':
            for bus in ['pci', 'pciecam', 'vmpci']:
                vm_modules[bus+'.hidd'] = 'Devs/Drivers/'+bus+'.hidd'
    modules = vm_modules
components = [(name, sdk / 'AROS' / rel) for name, rel in modules.items()]
efi = a.efi.resolve()
programs = [sdk / 'AROS/System/Wanderer/Wanderer'] if a.profile == 'wanderer' else []
for path in [stage2, efi] + [path for _, path in components] + programs:
    if not path.is_file():
        p.error(f'missing build product: {path}')
output.mkdir(parents=True)
probe = output / 'ABIPROBE'
subprocess.run([str(root / 'tools/build-abi-probe'), '--sdk', str(sdk),
                '--crosstools', str(a.toolchain.resolve()),
                '--profile', 'platform' if a.profile == 'headless' else 'cli', str(probe)], check=True)
pci_probe = output / 'PCIPROBE'
pci_files = []
if a.pci_hidd:
    subprocess.run([str(root / 'tools/build-abi-probe'), '--sdk', str(sdk),
        '--crosstools', str(a.toolchain.resolve()), '--profile', 'pci', str(pci_probe)], check=True)
    pci_files = [(rel, sdk/'AROS'/rel) for rel in ['Libs/oop.library',
        'Devs/Drivers/hiddclass.hidd','Devs/Drivers/pci.hidd','Devs/Drivers/pciecam.hidd',
        'Devs/Drivers/vmpci.hidd']]
if a.virtio_device:
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['Devs/Drivers/bus.hidd',
        'Devs/Drivers/storage.hidd','Devs/virtio.device']]
if a.virtionet_device:
    pci_files += [('Devs/Networks/virtionet.device',sdk/'AROS/Devs/Networks/virtionet.device')]
if a.network_stack:
    for name in ['AROSTCP','dhclient']:
        pci_files.append(('System/Network/AROSTCP/C/'+name,sdk/'AROS/System/Network/AROSTCP/C'/name))
    # Keep the standard tree: DHCP defaults and AROSTCP ProgramDir agree.
    configs={
        'general.config':'USELOOPBACK=YES\nDEBUGSANA=NO\nGATEWAY=NO\nHOSTNAME=macaros-vm\nOPENGUI=NO\n',
        'interfaces':'vn0 DEV=DEVS:Networks/virtionet.device UNIT=0 IP=DHCP NOTRACKING UP\n',
        'netdb':'WITH interfaces PREFIX=I\n',
    }
    for name,content in configs.items():
        config_file=output/('net-'+name);config_file.write_text(content)
        pci_files.append(('System/Network/AROSTCP/db/'+name,config_file))
if a.xhci_driver:
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['Libs/poseidon.library','Devs/USBHardware/pcixhci.device']]
    request = output/'XHCITEST.REQ'
    request.write_text('xhci-driver-startup-v1\n')
    pci_files += [('XHCITEST.REQ', request)]
if a.xhci_enumerate:
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['Classes/USB/hub.class','Classes/USB/hubss.class']]
    request = output/'XHCIENUM.REQ'
    request.write_text('xhci-child-enumeration-v1\n')
    pci_files += [('XHCIENUM.REQ', request)]
if a.usb_desktop:
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['Libs/poseidon.library',
        'Devs/USBHardware/pcixhci.device','Classes/USB/hub.class',
        'Classes/USB/hubss.class','Classes/USB/hid.class']]
ramdisk = output / 'SYS.img'
image_command = ['python3', str(root / 'tools/make-fat12-image.py'), '--abi-probe', str(probe)]
if a.pci_hidd:
    image_command += ['--file', f'PCIPROBE={pci_probe}']
for rel, path in pci_files:
    if not (a.network_stack and rel.startswith('System/Network/')):
        image_command += ['--file', f'{rel}={path}']
if a.startup_sequence:
    startup=output/'Startup-Sequence'
    shutil.copyfile(a.startup_sequence,startup)
    image_command += ['--startup-sequence',str(startup)]
    pci_files += [('Startup-Sequence',startup)]
if a.usb_desktop:
    usb_command=output/'VMUSB'
    subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
        '--crosstools',str(a.toolchain.resolve()),'--profile','usb-desktop',str(usb_command)],check=True)
    startup=output/'Startup-Sequence'
    startup.write_text('SYS:VMUSB\n'+('Run >NIL: SYS:VMINPUT\n' if a.input_probe else '')+'G4Ready\nEndCLI\n')
    if a.input_probe:
        input_command=output/'VMINPUT'
        subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
            '--crosstools',str(a.toolchain.resolve()),'--profile','desktop-input',str(input_command)],check=True)
        image_command += ['--file',f'VMINPUT={input_command}']
        pci_files += [('VMINPUT',input_command)]
    image_command += ['--file',f'VMUSB={usb_command}','--startup-sequence',str(startup)]
    pci_files += [('VMUSB',usb_command),('Startup-Sequence',startup)]
if programs:
    image_command += ['--wanderer', str(programs[0])]
seed=output/'SYS-seed.img' if a.network_stack else ramdisk
subprocess.run(image_command + [str(seed)], check=True)
subprocess.run(image_command + ['--verify', str(seed)], check=True)
if a.network_stack:
    expand=['python3',str(root/'tools/expand-system-image.py'),'--seed',str(seed),'--output',str(ramdisk)]
    for rel,path in pci_files:
        if rel.startswith('System/Network/'):expand += ['--file',f'{rel}={path}']
    subprocess.run(expand,check=True)
bundle = runpy.run_path(str(root / 'tools/arosbundle'))
args = SimpleNamespace(kernel=stage2, kernel_name='aros-apple-stage2',
                       kernel_memory_size=16 * 1024 * 1024, component=components,
                       ramdisk=ramdisk, ramdisk_name='SYS')
data = bundle['create_bundle'](args)
entries = bundle['verify_bundle'](data)
(output / 'AROS').mkdir()
(output / 'EFI/BOOT').mkdir(parents=True)
if a.desktop_session is not None:
    (output/'AROS/SESSION.CFG').write_text(a.desktop_session+'\n')
destination = output / 'AROS/GEN0.BND'
destination.write_bytes(data)
subprocess.run(['python3', str(root / 'tools/boot_generation.py'), 'create',
                '--bundle', str(destination), '--sequence', '1', '--slot', '0',
                '--state', 'accepted', '--output', str(output / 'AROS/BOOT0.ID')], check=True)
shutil.copyfile(efi, output / 'EFI/BOOT/BOOTAA64.EFI')
inputs = ([output/'AROS/SESSION.CFG'] if a.desktop_session is not None else []) + ([pci_probe] if a.pci_hidd else []) + [path for _, path in pci_files] + [stage2, efi, probe, ramdisk] + [path for _, path in components] + programs
manifest = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
(output / 'provenance.json').write_text(json.dumps({
    'profile': a.profile, 'network_stack': a.network_stack, 'virtionet_device': a.virtionet_device, 'input_probe': a.input_probe, 'desktop_session': a.desktop_session, 'usb_desktop': a.usb_desktop, 'virtio_gpu': a.virtio_gpu, 'xhci_driver': a.xhci_driver, 'xhci_enumerate': a.xhci_enumerate, 'build': str(build), 'seed_bundle': None,
    'source_revision': revision, 'configure_command': configure_command,
    'inputs': manifest, 'bundle_sha256': hashlib.sha256(data).hexdigest(),
}, indent=2) + '\n')
print(f'package-core result=PASS modules={len(entries)} output={output}')
