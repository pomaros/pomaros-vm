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
p.add_argument('--work-probe', action='store_true')
p.add_argument('--work-volume', action='store_true', help='mount the appended 256 MiB WORK volume made by make-disk --work-volume')
p.add_argument('--virtionet-device', action='store_true')
p.add_argument('--host-services', action='store_true')
p.add_argument('--clipboard-sync', action='store_true')
p.add_argument('--file-commands', action='store_true')
p.add_argument('--command-startup', action='store_true')
p.add_argument('--vsock-device', action='store_true')
p.add_argument('--network-dns', action='store_true')
p.add_argument('--network-probe', action='store_true')
p.add_argument('--network-stack', action='store_true')
p.add_argument('--network-echo-request', type=Path)
p.add_argument('--startup-sequence', type=Path)
p.add_argument('--desktop-session', help='1..3600 seconds or forever; omitted for boot proof')
p.add_argument('--clipboard-probe',action='store_true')
p.add_argument('--input-probe',action='store_true')
p.add_argument('--usb-desktop', action='store_true')
p.add_argument('--xhci-driver', action='store_true')
p.add_argument('--xhci-enumerate', action='store_true')
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--profile', choices=['headless', 'wanderer'], default='headless')
p.add_argument('--toolchain', type=Path, default=Path.home() / 'aros-crosstools')
p.add_argument('--efi', type=Path, required=True, help='explicit built UEFI loader; no implicit stale build selection')
a = p.parse_args()
if a.clipboard_sync:
    if a.profile!='headless': p.error('--clipboard-sync is the headless qualification; use --host-services for the desktop')
    a.file_commands=True
if a.file_commands:
    a.host_services=True;a.virtio_device=True
if a.command_startup and (a.profile!='headless' or a.host_services or a.vsock_device):
    p.error('--command-startup requires headless without host services or vsock device')
if a.host_services: a.vsock_device=True
if a.vsock_device and not (a.pci_hidd or (a.profile=='wanderer' and a.virtio_gpu)):
    p.error('--vsock-device requires --pci-hidd or a resident virtio-GPU desktop')
if a.network_echo_request:
    if len(a.network_echo_request.read_bytes()) != 38:
        p.error('network echo request must be 38 bytes')
    a.network_stack=True
if a.network_probe:
    if a.profile!='wanderer' or not a.desktop_session: p.error('--network-probe requires a desktop session')
    a.network_dns=True
if a.network_dns: a.network_stack=True
if a.network_stack:
    a.virtionet_device=True
if a.clipboard_probe and not (a.host_services and a.profile=='wanderer' and a.desktop_session):
    p.error('--clipboard-probe requires a desktop host-service session')
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
if a.work_probe and not (a.work_volume and a.desktop_session):
    p.error('--work-probe requires --work-volume and a desktop session')
if a.work_volume and not (a.profile=='wanderer' and a.virtio_gpu):
    p.error('--work-volume requires a resident virtio-GPU desktop')
if a.virtio_device and not a.pci_hidd:
    p.error('--virtio-device requires --pci-hidd')
if a.virtionet_device and not (a.pci_hidd or (a.profile=='wanderer' and a.virtio_gpu)):
    p.error('--virtionet-device requires --pci-hidd or resident virtio-GPU desktop')
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
if a.command_startup:
    request=output/'COMMAND.REQ';request.write_text('command-startup-v1\n')
    pci_files += [('COMMAND.REQ',request),('C/VMExchange',sdk/'AROS/C/VMExchange')]
if a.virtio_device or a.work_volume:
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['Devs/Drivers/bus.hidd',
        'Devs/Drivers/storage.hidd','Devs/virtio.device']]
if a.virtionet_device:
    pci_files += [('Devs/Networks/virtionet.device',sdk/'AROS/Devs/Networks/virtionet.device')]
if a.host_services:
    pci_files.append(('C/VMExchange',sdk/'AROS/C/VMExchange'))
    if a.profile=='headless':
        request=output/'HOSTSERVICES.REQ';request.write_text('file-services-v1\n')
        pci_files.append(('HOSTSERVICES.REQ',request))
    else:
        pci_files += [('C/VMSync',sdk/'AROS/C/VMSync'),('Devs/clipboard.device',sdk/'AROS/Devs/clipboard.device')]
if a.file_commands:
    request=output/'FILECOMMANDS.REQ';request.write_text('file-commands-v1\n')
    pci_files.append(('FILECOMMANDS.REQ',request))
if a.clipboard_sync:
    request=output/'CLIPBOARD.REQ';request.write_text('clipboard-sync-v1\n')
    pci_files += [('CLIPBOARD.REQ',request),('C/VMSync',sdk/'AROS/C/VMSync'),('Devs/clipboard.device',sdk/'AROS/Devs/clipboard.device')]
if a.vsock_device:
    pci_files.append(('Devs/vmbridge.device',sdk/'AROS/Devs/vmbridge.device'))
if a.network_stack:
    # NETTRACE initializes ARexx and Commodities; Commodities needs keymap.
    for name in ['rexxsyslib','commodities','keymap']:
        pci_files.append(('Libs/'+name+'.library',sdk/'AROS/Libs'/(name+'.library')))
    for name in ['AROSTCP','dhclient']:
        pci_files.append(('System/Network/AROSTCP/C/'+name,sdk/'AROS/System/Network/AROSTCP/C'/name))
    # Keep the standard tree: DHCP defaults and AROSTCP ProgramDir agree.
    configs={
        'general.config':'USELOOPBACK=YES\nDEBUGSANA=NO\nGATEWAY=NO\nHOSTNAME=macaros-vm\nOPENGUI=NO\n',
        'interfaces':'vn0 DEV=DEVS:Networks/virtionet.device UNIT=0 IP=DHCP NOTRACKING UP\n',
        'netdb':'WITH interfaces PREFIX=I\n',
    }
    if a.profile=='wanderer':
        configs['general.config'] += 'LOGFILENAME=T:AROSTCP.log\n'
    for name,content in configs.items():
        config_file=output/('net-'+name);config_file.write_text(content)
        pci_files.append(('System/Network/AROSTCP/db/'+name,config_file))
if a.network_dns:
    request=output/'DNS.REQ';request.write_text('dhcp-dns-example-com-v1\n')
    pci_files.append(('DNS.REQ',request))
if a.network_echo_request:
    pci_files.append(('NETECHO.REQ',a.network_echo_request))
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
if a.profile=='wanderer':
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['C/Assign','C/MakeDir']]
if a.work_volume:
    pci_files += [(rel,sdk/'AROS'/rel) for rel in ['C/Mount','C/LoadResource','L/fat-handler']]
    mountlist=output/'WORK.mount'
    mountlist.write_text('VMWORK0:\nFileSystem = L:fat-handler\nDevice = virtio.device\nUnit = 0\n'
        'BlockSize = 512\nSurfaces = 1\nBlocksPerTrack = 1\nLowCyl = 131072\nHighCyl = 655359\n'
        'Buffers = 64\nBufMemType = 1\nMaxTransfer = 0x100000\n'
        'DosType = 0x46415401\nStackSize = 65536\nActivate = 1\n#\n')
    pci_files.append(('Devs/WORK.mount',mountlist))
    if a.work_probe:
        command=output/'VMWORK'
        subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
            '--crosstools',str(a.toolchain.resolve()),'--profile','desktop-work',str(command)],check=True)
        pci_files.append(('VMWORK',command))
desktop_services = a.host_services and a.profile=='wanderer'
expanded_system = a.network_stack or a.profile=='wanderer'
def deferred_file(rel):
    return (a.work_volume and rel in {'VMWORK','C/Mount','C/LoadResource','L/fat-handler','Devs/WORK.mount','Devs/Drivers/bus.hidd','Devs/Drivers/storage.hidd','Devs/virtio.device'}) or (a.profile=='wanderer' and rel in {'C/Assign','C/MakeDir'}) or (a.network_stack and (rel.startswith('System/Network/') or rel in {'VMNET','Libs/rexxsyslib.library','Libs/commodities.library','Libs/keymap.library','Devs/Networks/virtionet.device'})) or (desktop_services and rel in {
        'C/VMExchange','C/VMSync','Devs/clipboard.device','Devs/vmbridge.device','VMCLIP'})
ramdisk = output / 'SYS.img'
image_command = ['python3', str(root / 'tools/make-fat12-image.py'), '--abi-probe', str(probe)]
if a.pci_hidd:
    image_command += ['--file', f'PCIPROBE={pci_probe}']
for rel, path in pci_files:
    if not deferred_file(rel):
        image_command += ['--file', f'{rel}={path}']
startup_lines = None
if a.startup_sequence:
    startup_lines=a.startup_sequence.read_text()
if a.usb_desktop:
    usb_command=output/'VMUSB'
    subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
        '--crosstools',str(a.toolchain.resolve()),'--profile','usb-desktop',str(usb_command)],check=True)
    startup_lines='SYS:VMUSB\n'+('Run >NIL: SYS:VMINPUT\n' if a.input_probe else '')+'G4Ready\nEndCLI\n'
    if a.input_probe:
        input_command=output/'VMINPUT'
        subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
            '--crosstools',str(a.toolchain.resolve()),'--profile','desktop-input',str(input_command)],check=True)
        image_command += ['--file',f'VMINPUT={input_command}']
        pci_files += [('VMINPUT',input_command)]
    image_command += ['--file',f'VMUSB={usb_command}']
    pci_files.append(('VMUSB',usb_command))
if a.host_services and a.profile=='wanderer':
    startup_lines='Run >NIL: SYS:C/VMSync\n'+(startup_lines or 'G4Ready\nEndCLI\n')
if a.clipboard_probe:
    clipboard_command=output/'VMCLIP'
    subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
        '--crosstools',str(a.toolchain.resolve()),'--profile','desktop-clipboard',str(clipboard_command)],check=True)
    # Start after VMSync without making desktop startup depend on the probe.
    startup_lines=startup_lines.replace('Run >NIL: SYS:C/VMSync\n',
        'Run >NIL: SYS:C/VMSync\nRun >NIL: SYS:VMCLIP\n',1)
    pci_files.append(('VMCLIP',clipboard_command))
if a.network_stack and a.profile=='wanderer':
    startup_lines='Run >T:AROSTCP-startup.log SYS:System/Network/AROSTCP/C/AROSTCP\n'+(startup_lines or 'G4Ready\nEndCLI\n')
if a.network_probe:
    network_command=output/'VMNET'
    subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),
        '--crosstools',str(a.toolchain.resolve()),'--profile','desktop-network',str(network_command)],check=True)
    startup_lines=startup_lines.replace('Run >T:AROSTCP-startup.log SYS:System/Network/AROSTCP/C/AROSTCP\n',
        'Run >T:AROSTCP-startup.log SYS:System/Network/AROSTCP/C/AROSTCP\nRun >NIL: SYS:VMNET\n',1)
    pci_files.append(('VMNET',network_command))
if a.work_volume:
    startup_lines='SYS:C/LoadResource DEVS:Drivers/bus.hidd DEVS:Drivers/storage.hidd\nSYS:C/Mount VMWORK0: FROM DEVS:WORK.mount\n'+(startup_lines or 'G4Ready\nEndCLI\n')
if a.work_volume:
    # FAT replies to handler startup before publishing the volume name.
    # Lock through the device once, then release it before desktop use.
    startup_lines=startup_lines.replace('SYS:C/Mount VMWORK0: FROM DEVS:WORK.mount\n',
        'SYS:C/Mount VMWORK0: FROM DEVS:WORK.mount\nSYS:C/Assign VMWorkReady: VMWORK0:\nSYS:C/Assign VMWorkReady: REMOVE\n',1)
if a.work_probe:
    startup_lines=startup_lines.replace('SYS:C/Assign VMWorkReady: REMOVE\n',
        'SYS:C/Assign VMWorkReady: REMOVE\nSYS:VMWORK\n',1)
if a.profile=='wanderer':
    startup_lines='SYS:C/MakeDir RAM:T\nSYS:C/Assign T: RAM:T\n'+(startup_lines or 'G4Ready\nEndCLI\n')
if startup_lines is not None:
    startup=output/'Startup-Sequence'
    startup.write_text(startup_lines)
    image_command += ['--startup-sequence',str(startup)]
    pci_files.append(('Startup-Sequence',startup))
if programs:
    image_command += ['--wanderer', str(programs[0])]
seed=output/'SYS-seed.img' if expanded_system else ramdisk
subprocess.run(image_command + [str(seed)], check=True)
subprocess.run(image_command + ['--verify', str(seed)], check=True)
if expanded_system:
    expand=['python3',str(root/'tools/expand-system-image.py'),'--seed',str(seed),'--output',str(ramdisk)]
    for rel,path in pci_files:
        if deferred_file(rel):expand += ['--file',f'{rel}={path}']
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
    'profile': a.profile, 'work_volume': a.work_volume, 'work_probe': a.work_probe, 'clipboard_probe': a.clipboard_probe, 'clipboard_sync': a.clipboard_sync, 'file_commands': a.file_commands, 'command_startup': a.command_startup, 'vsock_device': a.vsock_device, 'host_services': a.host_services, 'network_dns': a.network_dns, 'network_probe': a.network_probe, 'network_stack': a.network_stack, 'virtionet_device': a.virtionet_device, 'input_probe': a.input_probe, 'desktop_session': a.desktop_session, 'usb_desktop': a.usb_desktop, 'virtio_gpu': a.virtio_gpu, 'xhci_driver': a.xhci_driver, 'xhci_enumerate': a.xhci_enumerate, 'build': str(build), 'seed_bundle': None,
    'source_revision': revision, 'configure_command': configure_command,
    'inputs': manifest, 'bundle_sha256': hashlib.sha256(data).hexdigest(),
}, indent=2) + '\n')
print(f'package-core result=PASS modules={len(entries)} output={output}')
