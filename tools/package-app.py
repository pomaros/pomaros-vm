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

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--esp',type=Path,required=True,help='normal package-core output, with WORK and host services')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True):
    p.error('commit or resolve VM source changes before recording an application')
if a.output.suffix!='.app' or a.output.exists(): p.error('output must be a new .app directory')
manifest=json.loads((a.esp/'provenance.json').read_text())
if not (manifest.get('profile')=='wanderer' and manifest.get('work_volume') and
        manifest.get('host_services') and manifest.get('network_stack') and
        manifest.get('usb_desktop') and manifest.get('desktop_session')=='forever'):
    p.error('requires a normal live desktop with WORK, USB, host services and networking')
if any(manifest.get(k) for k in ['work_probe','shutdown_probe','clipboard_probe','network_probe','input_probe']):
    p.error('qualification probes must not be included in the application')
if 'SYS:C/VMSync VOLUME=VMWORK0:' not in (a.esp/'Startup-Sequence').read_text():
    p.error('guest shutdown participation is required')
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
