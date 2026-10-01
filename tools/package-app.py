#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Build a private, ad-hoc signed Macaros VM app with an immutable seed disk."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess

DESKTOP_FEATURES = ('virtio_gpu', 'work_volume', 'host_services',
                    'network_stack', 'usb_desktop')
QUALIFICATION_PROBES = ('work_probe', 'shutdown_probe', 'clipboard_probe',
                        'network_probe', 'input_probe', 'soak_probe')


def validate_desktop(manifest, startup):
    """Reject incomplete or qualification-only payloads before creating an app."""
    if (manifest.get('profile') != 'wanderer'
            or manifest.get('desktop_session') != 'forever'
            or not all(manifest.get(feature) for feature in DESKTOP_FEATURES)):
        raise ValueError('requires a live GPU desktop with WORK, USB, host services and networking')
    if any(manifest.get(probe) for probe in QUALIFICATION_PROBES):
        raise ValueError('qualification probes must not be included in the application')
    if 'SYS:C/VMSync VOLUME=VMWORK0:' not in startup:
        raise ValueError('guest shutdown participation is required')


def main():
    root=Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--esp',type=Path,required=True,help='normal package-core output, with WORK and host services')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True):
        p.error('commit or resolve VM source changes before recording an application')
    if a.output.suffix!='.app' or a.output.exists(): p.error('output must be a new .app directory')
    manifest=json.loads((a.esp/'provenance.json').read_text())
    try:
        validate_desktop(manifest, (a.esp/'Startup-Sequence').read_text())
    except ValueError as error:
        p.error(str(error))
    launcher=root/'build/macaros-vm'
    if not launcher.is_file(): p.error('build the launcher first')
    a.output.mkdir(parents=True)
    try:
        contents=a.output/'Contents';binary=contents/'MacOS';resources=contents/'Resources'
        binary.mkdir(parents=True);resources.mkdir()
        shutil.copy2(launcher,binary/'MacarosVM')
        subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(a.esp),
                        '--output',str(resources/'System.img')],check=True)
        info={'CFBundleIdentifier':'me.jkn.macaros-vm','CFBundleName':'Macaros VM',
              'CFBundleDisplayName':'Macaros VM','CFBundleExecutable':'MacarosVM',
              'CFBundlePackageType':'APPL','CFBundleShortVersionString':'0.1.0',
              'CFBundleVersion':'1','LSMinimumSystemVersion':'13.0','NSHighResolutionCapable':True}
        (contents/'Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True))
        provenance={'source_revision':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
                    'guest':manifest,'launcher_sha256':hashlib.sha256(launcher.read_bytes()).hexdigest(),
                    'seed_sha256':hashlib.sha256((resources/'System.img').read_bytes()).hexdigest()}
        (resources/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
        shutil.copy2(root/'LICENSE',resources/'HOST-LICENSE')
        guest_source=Path(manifest['configure_command'][0]).parent
        for name in ['LICENSE','LICENSE.GPL','LICENSE.LGPL','LICENSE.Author','LEGAL']:
            shutil.copy2(guest_source/name,resources/('AROS-'+name))
        subprocess.run(['codesign','--force','--sign','-','--entitlements',str(root/'entitlements.plist'),str(a.output)],check=True)
        subprocess.run(['codesign','--verify','--strict',str(a.output)],check=True)
    except BaseException:
        shutil.rmtree(a.output);raise
    print(f'package-app result=PASS path={a.output} signing=ad-hoc distribution=private')


if __name__ == '__main__':
    main()
