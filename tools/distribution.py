#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Build/package the standard AROS runtime for PomAROS VM (no hosted binaries)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import time
from types import SimpleNamespace
from fat_metadata import EPOCH, normalize_fat16

ROOT = Path(__file__).resolve().parents[1]
# Runtime boundary derived from PomAROS/graft/make-aros-release.sh. Platform
# boot modules come from package-core, never from the hosted PomAROS directory.
ROOTS = 'C Classes Devs Extras Fonts L Libs Locale Prefs S Storage System Tools Utilities WBStartup'.split()
EXCLUDED = {'Zed', 'ZedAros', 'Ferail', 'Feraille', 'Moonstone', 'Developer',
            'AppleCPU', 'AppleCopy', 'AppleDisk', 'AppleFiles', 'ApplePCIeDiag',
            'AppleWrite', 'PkgRecv', 'GPIO', 'Install-grub', 'Install-grub2'}
# Public font payloads use only these reviewed families and their notices.
FONT_SOURCE_FAMILIES = {'barlow', 'bellefair', 'bitstream', 'opensans', 'spectral', 'work-sans'}
FONT_FILE_PATTERN = re.compile(r'(?:Barlow-|Bellefair-|OpenSans-|Spectral-|WorkSans-|Vera)[A-Za-z0-9-]+\.ttf$')
FONT_SELECTOR_PREFIXES = ('Barlow ', 'Bellefair ', 'Open Sans ', 'Spectral ', 'Vera ', 'Work Sans ')
REQUIRED = ['Libs/jfif.library', 'Libs/png.library', 'Libs/z1.library', 'Libs/freetype2.library', 'Libs/kms.library', 'C/DeviceTree', 'C/CPUInfo', 'C/LoadKeymap', 'C/ConClip', 'System/FixFonts', 'System/Network/AROSTCP/C/AROSTCP', 'C/Copy', 'C/List', 'C/Execute', 'C/AddDataTypes',
            'C/Assign', 'C/MakeDir', 'C/Mount', 'C/LoadResource',
            'L/pipe-handler', 'L/afs-handler', 'L/sfs-handler', 'C/IPrefs', 'C/VMExchange', 'C/VMSync', 'System/Wanderer/Wanderer',
            'Tools/Editor', 'Tools/Calculator', 'Utilities/Clock',
            'Utilities/MultiView', 'Utilities/More', 'Prefs/ScreenMode',
            'Prefs/Locale', 'Prefs/Input', 'S/Shell-Startup', 'Devs/Keymaps/pc104_usa', 'Devs/Keymaps/pc105_f', 'Libs/datatypes.library',
            'Classes/DataTypes/picture.datatype', 'Classes/DataTypes/ilbm.datatype',
            'Classes/DataTypes/png.datatype', 'Classes/DataTypes/jpeg.datatype',
            'Prefs/Presets/Themes/AROSDefault/system/config', 'Fonts/Open Sans Regular.font']

def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)

def config(build):
    sdk = build / 'bin/apple-aarch64/AROS'
    cfg = (sdk.parent / 'gen/config/target.cfg').read_text(encoding='latin-1')
    if 'AROS_VM_GUEST           := 1' not in cfg or '-ffixed-x18' in cfg:
        raise ValueError('requires a VM-configured AArch64 build with ordinary x18')
    source = Path(json.loads((build / 'vm-configure-command.json').read_text())[0]).parent
    expected = json.loads((build / 'vm-build-environment.json').read_text())['source_revision']
    actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != expected:
        raise ValueError('core source changed after configure; select a fresh matching build')
    return sdk, source

def build_runtime(a):
    _, source = config(a.build)
    modules = subprocess.check_output(['git', '-C', str(source), 'config', '--file', '.gitmodules', '--get-regexp', r'^submodule\..*\.(path|url)$'], text=True)
    modules = dict(line.split(' ', 1) for line in modules.splitlines())
    # Some catalogs live in "cat" or "translations", not "catalogs".
    paths = [modules[key[:-3] + 'path'] for key, url in modules.items()
             if key.endswith('.url') and url.startswith('https://github.com/aros-translation-team/')]
    run(['git', '-C', source, 'submodule', 'update', '--init', '--jobs', '4', '--', *paths])
    targets = json.loads((ROOT / 'distribution/targets.json').read_text())
    batch = a.build / 'vm-distribution.mk'
    batch.write_text('.PHONY: vm-distribution\nvm-distribution:\n\t@+$(CALL) $(MMAKE) $(MMAKE_OPTIONS) ' +
                     ' '.join('AROS.' + target for target in targets) + '\n')
    log = a.build / 'vm-distribution.log'
    if log.exists():
        log.rename(log.with_name('vm-distribution-' + str(time.time_ns()) + '.log'))
    with log.open('w') as stream:
        result = subprocess.run(['make', '-C', str(a.build), '-j' + str(a.jobs), '-f', 'Makefile', '-f', str(batch), 'vm-distribution'], stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode or 'Nothing known about' in log.read_text(errors='replace'):
        raise RuntimeError('distribution build failed: ' + str(log))
    print('distribution build PASS: ' + str(log))

# Only portable and VM device payloads are shipped on the runtime volume.
DEVICE_TREES = {'DataTypes', 'Keymaps', 'Printers'}
DEVICE_FILES = {'ahi.device', 'clipboard.device', 'console.device',
    'gameport.device', 'input.device', 'keyboard.device', 'timer.device',
    'FileSystem.resource', 'task.resource', 'shell.resource',
    'shellcommands.resource', 'lddemon.resource', 'pci.ids',
    'virtio.device', 'vmbridge.device', 'WORK.mount',
    'Drivers/bus.hidd', 'Drivers/storage.hidd', 'Drivers/vmpci.hidd',
    'Drivers/virtiogfx.hidd', 'Drivers/pci.hidd', 'Drivers/pciecam.hidd',
    'Drivers/gfx.hidd', 'Drivers/hiddclass.hidd', 'Drivers/inputclass.hidd',
    'Drivers/keyboard.hidd', 'Drivers/mouse.hidd',
    'USBHardware/pcixhci.device', 'Networks/virtionet.device',
    'DOSDrivers/PIPE', 'DOSDrivers/AUDIO', 'AHI/void.audio'}

def permitted(path):
    if path.parts[0] == 'Fonts':
        name = path.name.removesuffix('.info')
        if len(path.parts)>1 and path.parts[1]=='TrueType' and path.suffix.lower() in {'.ttf','.otf'} and not FONT_FILE_PATTERN.fullmatch(name):
            return False
        if path.suffix.lower() in {'.font','.otag'} and not name.startswith(FONT_SELECTOR_PREFIXES):
            return False
    if path.parts[0] == 'Devs' and len(path.parts) > 1:
        rel = Path(*path.parts[1:]).as_posix()
        if path.parts[1] not in DEVICE_TREES and not any(
                item == rel or item.startswith(rel + '/') for item in DEVICE_FILES):
            return False
    return (not any(part.startswith('.') for part in path.parts) and
            not any(part.removesuffix('.info') in EXCLUDED for part in path.parts) and
            not path.name.startswith(('mmakefile', 'Makefile')) and
            path.suffix.lower() not in {'.o', '.a', '.dbg', '.snapshot', '.ips', '.core'})

def copy_runtime(sdk, destination):
    for name in ROOTS:
        top = sdk / name
        if not top.is_dir():
            continue
        for path in sorted(top.rglob('*')):
            rel = path.relative_to(sdk)
            if not permitted(rel):
                continue
            if path.is_symlink():
                raise ValueError('runtime symlink must be resolved explicitly: ' + str(rel))
            target = destination / rel
            if path.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            elif path.is_file():
                data = path.read_bytes()
                if data[:4] == b'\x7fELF' and (data[4:6] != b'\x02\x01' or int.from_bytes(data[18:20], 'little') != 183):
                    raise ValueError('non-AArch64 runtime executable: ' + str(rel))
                if data[:4] in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe'):
                    raise ValueError('host Mach-O in runtime: ' + str(rel))
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
    for name in ROOTS:
        icon = sdk / (name + '.info')
        if icon.is_file():
            shutil.copy2(icon, destination / icon.name)


def install_iconset(system):
    """Install the selected preset's defaults and icons for shipped objects."""
    name = (system / 'Prefs/Env-Archive/SYS/iconset.var').read_text().strip()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', name):
        raise ValueError('invalid iconset name')
    source = system / 'Prefs/Presets/Icons' / name / 'Default/AROS'
    if not source.is_dir() or source.is_symlink():
        raise ValueError('selected iconset is missing: ' + name)
    installed = []
    for icon in sorted(source.rglob('*')):
        if icon.is_symlink() or not (icon.is_file() or icon.is_dir()):
            raise ValueError('invalid iconset entry: ' + str(icon))
        if not icon.is_file():
            continue
        rel = icon.relative_to(source)
        if icon.suffix.lower() != '.info':
            raise ValueError('non-icon file in selected iconset: ' + str(rel))
        default = (rel.parts[:3] == ('Prefs', 'Env-Archive', 'SYS')
                   and icon.name.startswith('def_'))
        target = system / rel
        if not default and not target.with_suffix('').exists():
            continue  # Do not expose icons for programs omitted from this VM.
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(icon, target)
        installed.append(rel.as_posix())
    if not {'Prefs/Env-Archive/SYS/def_Disk.info',
            'Prefs/Env-Archive/SYS/def_Drawer.info',
            'Prefs/Env-Archive/SYS/def_Tool.info'} <= set(installed):
        raise ValueError('selected iconset lacks required desktop defaults')
    return installed

def package(a):
    sdk, source = config(a.build)
    configure = json.loads((a.build / 'vm-configure-command.json').read_text())
    prefix = '--with-aros-toolchain-install='
    installations = [argument[len(prefix):] for argument in configure if argument.startswith(prefix)]
    if len(installations) != 1:
        raise ValueError('configure identity must name one installed guest toolchain')
    toolchain = Path(installations[0]).resolve()
    if not (toolchain / 'bin/clang').is_file():
        raise ValueError('configured guest toolchain is unavailable: ' + str(toolchain))
    if a.output.exists():
        raise ValueError('output must be new; existing distributions and user disks are never overwritten')
    required = [name for name in REQUIRED if a.backend == 'vz' or name not in {'C/VMExchange', 'C/VMSync'}]
    missing = [name for name in required if not (sdk / name).is_file()]
    if missing:
        raise ValueError('build the distribution first; missing ' + ', '.join(missing))
    a.output.mkdir(parents=True)
    base = a.output / 'base'
    run(['python3', ROOT / 'tools/package-core.py', '--build', a.build, '--efi', a.efi,
         '--toolchain', toolchain, '--output', base, '--profile', 'wanderer', '--virtio-gpu', '--usb-desktop',
         '--work-volume', '--backend', a.backend, '--network-stack', '--desktop-session', 'desktop']
         + (['--host-services'] if a.backend == 'vz' else []))
    esp = a.output / 'esp'
    shutil.copytree(base, esp)
    system = esp / 'WORK/System'
    system.mkdir(parents=True)
    copy_runtime(sdk, system)
    if a.backend == 'qemu':
        for name in ('C/VMExchange', 'C/VMSync', 'Devs/vmbridge.device'):
            (system / name).unlink(missing_ok=True)
    # The boot seed owns VM-specific commands, mountlists and network settings.
    env = dict(os.environ, LC_ALL='en_US.UTF-8', TZ='UTC')
    run(['mcopy', '-o', '-s', '-i', base / 'SYS.img', '::*', system], env=env, stdout=subprocess.DEVNULL)
    for name in ROOTS:
        (system / name).mkdir(exist_ok=True)
    (system / 'Devs/Printers').mkdir(parents=True, exist_ok=True)
    (system / 'Devs/DOSDrivers').mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'workbench/devs/DOSDrivers/PIPE', system / 'Devs/DOSDrivers/PIPE')
    (system / 'Prefs/Env-Archive').mkdir(parents=True, exist_ok=True)
    (system / 'Prefs/Env-Archive/HOME').write_text('WORK:')
    # LoadKeymap RESTORE applies this on first boot. Later choices made with
    # LoadKeymap replace the saved preference on the user's own WORK volume.
    (system / 'Prefs/Env-Archive/Keymap').write_text('pc105_f\n')
    installed_icons = install_iconset(system)
    print(f'distribution iconset installed={len(installed_icons)} '
          f'name={(system / "Prefs/Env-Archive/SYS/iconset.var").read_text().strip()}')
    shutil.copy2(ROOT / ('distribution/ReadMe.qemu' if a.backend=='qemu' else 'distribution/ReadMe'), system / 'ReadMe')
    (system / 'Prefs/Env-Archive/SYS').mkdir(exist_ok=True)
    (system / 'Prefs/Env-Archive/SYS/theme.var').write_text('THEMES:AROSDefault\n')
    (system / 'Prefs/Presets/theme.default').write_text('THEMES:AROSDefault\n')
    for family in sorted((source / 'workbench/fonts/truetype').iterdir()):
        if not family.is_dir() or family.name not in FONT_SOURCE_FAMILIES:
            continue
        for notice in sorted(family.iterdir()):
            if notice.is_file() and notice.name.lower().startswith(('license', 'copying', 'copyright', 'legal', 'ofl')):
                destination = system / 'Documentation/Fonts' / family.name / notice.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, destination)
    for name in ['LICENSE', 'LICENSE.GPL', 'LICENSE.LGPL', 'LICENSE.Author', 'LEGAL']:
        shutil.copy2(source / name, system / name)
    run(['python3', ROOT / 'tools/package-notices.py', '--source', source,
         '--output', system / 'Documentation/Licenses/libfdt', '--build', a.build])
    # This runs only after the existing storage readiness barrier. Explicit
    # assigns replace bootstrap RAM paths; modules already resident stay loaded.
    setup = '''FailAt 21
Echo >VMWORK0:distribution-startup.log "Switching SYS to disk"
SYS:C/Assign SYS: VMWORK0:System
SYS:C/Assign C: SYS:C
SYS:C/Assign S: SYS:S
SYS:C/Assign L: SYS:L
SYS:C/Assign LIBS: SYS:Libs
SYS:C/Assign LIBS: SYS:Classes ADD
SYS:C/Assign DEVS: SYS:Devs
SYS:C/Assign DRIVERS: SYS:Devs/Drivers
SYS:C/Assign LIBS: DRIVERS: ADD
SYS:C/Assign PRINTERS: DEVS:Printers
SYS:C/Assign REXX: S:
SYS:C/Assign KEYMAPS: DEVS:Keymaps
SYS:C/Assign FONTS: SYS:Fonts
SYS:C/Assign LOCALE: SYS:Locale
If NOT EXISTS RAM:Clipboards
    SYS:C/MakeDir RAM:Clipboards
EndIf
SYS:C/Assign CLIPS: RAM:Clipboards
SYS:C/Mount DEVS:DOSDrivers/PIPE
SYS:C/Assign ENVARC: SYS:Prefs/Env-Archive
SYS:C/Assign WANDERER: SYS:System/Wanderer
SYS:C/Assign HELP: LOCALE:Help DEFER
SYS:C/Assign THEMES: SYS:Prefs/Presets/Themes
SYS:C/Assign THEME: THEMES:AROSDefault
SYS:C/Assign IMAGES: SYS:System/Images DEFER
If EXISTS THEME:Images
    SYS:C/Assign IMAGES: THEME:Images PREPEND
EndIf
If NOT EXISTS RAM:ENV
    SYS:C/MakeDir RAM:ENV
EndIf
SYS:C/Assign ENV: RAM:ENV
SYS:C/Copy ENVARC: ENV: ALL QUIET
SYS:C/LoadKeymap RESTORE
SYS:C/SetEnv HOME WORK:
SYS:C/Path C: SYS:System S: SYS:Prefs SYS:Tools SYS:Utilities QUIET
CD SYS:
Echo >>VMWORK0:distribution-startup.log "Datatypes and preferences"
SYS:C/AddDataTypes REFRESH QUIET
Echo >>VMWORK0:distribution-startup.log "Datatypes ready"
If NOT EXISTS FONTS:fixed.font
    SYS:System/FixFonts
EndIf
Echo >>VMWORK0:distribution-startup.log "Fonts ready"
SYS:C/IPrefs
Run >NIL: SYS:C/ConClip
Echo >>VMWORK0:distribution-startup.log "Desktop setup complete"
'''
    startup = (base / 'Startup-Sequence').read_text()
    marker = 'SYS:C/Assign VMWorkReady: REMOVE\n'
    if startup.count(marker) != 1:
        raise ValueError('missing unique WORK readiness barrier')
    # Launch shutdown and input from the RAM bootstrap before optional setup.
    services = ('Run >NIL: SYS:C/VMSync VOLUME=VMWORK0:\n' if a.backend == 'vz' else '') + 'SYS:VMUSB\n'
    if startup.count(services) != 1:
        raise ValueError('missing unique VM service startup')
    startup = startup.replace(services, '')
    startup = startup.replace(marker, marker + services + setup)
    (esp / 'Startup-Sequence').write_text(startup)
    (system / 'S/Startup-Sequence').write_text(startup)
    # Keep boot and desktop startup identical. Changing only an outer ESP copy
    # would leave the RAM boot volume running the old startup.
    run(['mcopy', '-o', '-i', esp / 'SYS.img', esp / 'Startup-Sequence', '::S/Startup-Sequence'], env=env)
    (esp / 'SYS.img').write_bytes(normalize_fat16((esp / 'SYS.img').read_bytes()))
    bundle = runpy.run_path(str(ROOT / 'tools/arosbundle'))
    old = (base / 'AROS/GEN0.BND').read_bytes()
    entries = bundle['verify_bundle'](old)
    inputs = a.output / 'bundle-inputs'; inputs.mkdir()
    args = SimpleNamespace(component=[])
    for entry in entries:
        path = inputs / entry['name']
        path.write_bytes(old[entry['offset']:entry['offset'] + entry['size']])
        if entry['type'] == bundle['TYPE']['kernel']:
            args.kernel = path; args.kernel_name = entry['name']; args.kernel_memory_size = entry['memory_size']
        elif entry['type'] == bundle['TYPE']['ramdisk']:
            args.ramdisk = esp / 'SYS.img'; args.ramdisk_name = entry['name']
        else:
            args.component.append((entry['name'], path))
    data = bundle['create_bundle'](args)
    (esp / 'AROS/GEN0.BND').write_bytes(data)
    (esp / 'AROS/BOOT0.ID').unlink()
    run(['python3', ROOT / 'tools/boot_generation.py', 'create', '--bundle', esp / 'AROS/GEN0.BND',
         '--sequence', '1', '--slot', '0', '--state', 'accepted', '--output', esp / 'AROS/BOOT0.ID'])
    # Disk-local marker lets boot-only tooling reject partial distribution updates.
    (esp / 'AROS/DISTRIB.TAG').write_text('{"distribution": true, "system": "WORK:System"}\n')
    # Recheck after the bootstrap image merge: it cannot bypass font admission.
    for font in (system/'Fonts').rglob('*'):
        if font.is_file() and not permitted(font.relative_to(system)):
            raise ValueError('bootstrap merge introduced an unreviewed font: '+str(font))
    for requirement in [*required, 'Devs/DOSDrivers/PIPE']:
        if not (system / requirement).is_file():
            raise ValueError('missing staged runtime requirement: ' + requirement)
    inventory = {}
    for path in sorted(system.rglob('*')):
        os.utime(path, (EPOCH, EPOCH))
        if path.is_file():
            inventory[path.relative_to(system).as_posix()] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    manifest = json.loads((base / 'provenance.json').read_text())
    manifest.update(base_package=str(base), bootstrap_ramdisk_sha256=hashlib.sha256((esp/'SYS.img').read_bytes()).hexdigest(), startup_sha256=hashlib.sha256(startup.encode()).hexdigest(), distribution=True, distribution_location='WORK:System',
                    bundle_sha256=hashlib.sha256(data).hexdigest(), distribution_files=inventory,
                    font_source_families=sorted(FONT_SOURCE_FAMILIES),
                    excluded_font_files=[str(p.relative_to(sdk)) for p in sorted((sdk/'Fonts').rglob('*')) if p.is_file() and not permitted(p.relative_to(sdk))],
                    excluded_products=['Zed', 'Ferail', 'Moonstone', 'hosted macOS drivers', 'Developer SDK'])
    (esp / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('distribution package PASS files=' + str(len(inventory)) + ' bytes=' + str(sum(x['bytes'] for x in inventory.values())))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'package'])
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--backend', choices=['vz', 'qemu'], default='vz')
    parser.add_argument('--efi', type=Path)
    parser.add_argument('--output', type=Path)
    a = parser.parse_args(); a.build = a.build.resolve()
    if a.action == 'build':
        build_runtime(a)
    else:
        if not a.efi or not a.output:
            parser.error('package requires --efi and --output')
        a.efi = a.efi.resolve(); a.output = a.output.resolve()
        package(a)

if __name__ == '__main__':
    main()
