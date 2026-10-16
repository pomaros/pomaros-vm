#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Prepare signed apps, verify runtime identity, and notarize distributions."""
import argparse
import hashlib
import json
import plistlib
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
IDENTITY='Developer ID Application: John Knipper (C43N3NG7Z5)'

def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):digest.update(chunk)
    return digest.hexdigest()

def run(command,record=None):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if record:
        record.parent.mkdir(parents=True,exist_ok=True)
        record.with_suffix('.command.json').write_text(json.dumps(list(map(str,command)),indent=2)+'\n')
        record.write_text(result.stdout+result.stderr)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}): {command[0]}\n{result.stdout}{result.stderr}')
    return result.stdout,result.stderr

def write_json(path,value):
    path.write_text(json.dumps(value,indent=2)+'\n')

def clean_revision(repo):
    if run(['git','-C',repo,'status','--porcelain'])[0]:
        raise ValueError('commit product source before preparing distribution provenance')
    return run(['git','-C',repo,'rev-parse','HEAD'])[0].strip()

def verify_app(app,team,evidence,entitlements):
    run(['codesign','--verify','--strict','--verbose=2',app],evidence/'codesign-verify.log')
    stdout,stderr=run(['codesign','--display','--verbose=4',app],evidence/'codesign-details.log')
    details=stdout+stderr
    flags=re.search(r'flags=0x([0-9a-fA-F]+)',details)
    if f'TeamIdentifier={team}' not in details or 'Authority=Developer ID Application:' not in details or not flags or not(int(flags[1],16)&0x10000) or 'Timestamp=' not in details:
        raise ValueError('requires Developer ID, expected team, hardened runtime and secure timestamp')
    stdout,_=run(['codesign','--display','--entitlements',':-',app],evidence/'codesign-entitlements.log')
    if plistlib.loads(stdout.encode())!=plistlib.loads(entitlements.read_bytes()):
        raise ValueError('signed entitlements differ from the minimal product policy')
    return details

def guest_source_lock(path, guest):
    lock=dict(line.strip().split('=',1) for line in path.read_text().splitlines()
              if line.strip() and not line.startswith('#'))
    if (lock.get('CORE_REVISION') != guest['source_revision']
            or lock.get('CORE_X18_POLICY') != 'unrestricted'
            or lock.get('CORE_TARGET') != 'apple-aarch64'
            or not lock.get('CORE_URL') or not lock.get('CORE_BRANCH')):
        raise ValueError('guest source differs from source lock; retained historical payloads require --guest-source-lock with their actual URL/revision/ABI, not the current public source pin')
    recorded=guest.get('source_lock')
    if recorded and any(recorded.get(key) != lock.get(key) for key in
                        ('CORE_URL','CORE_REVISION','CORE_BRANCH','CORE_X18_POLICY','CORE_TARGET')):
        raise ValueError('selected source lock differs from the packaged guest source provenance')
    return lock

def prepare(a):
    if not a.app or not a.app.is_dir() or a.output.exists():raise ValueError('prepare requires an existing app and NEW output directory')
    revision=clean_revision(a.repo)
    if shutil.disk_usage(a.output.parent).free<2*1024**3:raise ValueError('less than 2 GiB free before distribution staging')
    original=a.app.resolve();resources=original/'Contents/Resources'
    original_provenance=json.loads((resources/'provenance.json').read_text())
    guest=original_provenance['guest']
    source_lock=guest_source_lock(a.guest_source_lock or a.repo/'CORE.lock',guest)
    if a.public_preview:
        if a.guest_source_lock or not a.release_tag or not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+-preview\.[0-9]+',a.release_tag):
            raise ValueError('public preview requires its release tag and current public core lock')
        if source_lock['CORE_URL']!='https://github.com/pomaros/aros-core.git':
            raise ValueError('public preview requires the curated public corresponding core source')
    if guest['backend']!=('vz' if a.product=='vm' else 'qemu') or guest.get('desktop_session')!='desktop':raise ValueError('requires a qualified production desktop package for this backend')
    for required in ('HOST-LICENSE','AROS-LICENSE','AROS-LEGAL','Licenses/libfdt'):
        if not(resources/required).exists():raise ValueError('missing guest/host notice: '+required)
    if a.public_preview:
        for required in ('Licenses/Unicode/UNICODE-LICENSE-V3.txt','Licenses/LLVM/LLVM-COMPILER-RT-LICENSE.txt','Licenses/Components/NOTICE.md'):
            if not (resources/required).is_file():raise ValueError('missing public component notice: '+required)
    if a.product=='qemu' and not list(resources.glob('*licenses*')):raise ValueError('missing QEMU firmware license inventory')
    a.output.mkdir(parents=True)
    app=a.output/original.name;run(['ditto','--rsrc','--extattr',original,app])
    info_path=app/'Contents/Info.plist';info=plistlib.loads(info_path.read_bytes())
    if a.product=='vm':
        info['CFBundleIdentifier']='org.pomaros.vm';info['PomAROSVMProfileName']='Default'
    version=info['CFBundleShortVersionString']
    if a.public_preview and not a.release_tag.startswith('v'+version+'-preview.'):
        raise ValueError('release tag differs from the packaged app version')
    info_path.write_bytes(plistlib.dumps(info,sort_keys=True))
    executable=app/'Contents/MacOS'/info['CFBundleExecutable']
    # There are no bundled dylibs/frameworks/helpers. Reject new nested code
    # rather than hiding unreviewed signing requirements behind --deep.
    magic={b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca'}
    native=[]
    for path in app.rglob('*'):
        if path.is_file():
            with path.open('rb') as source:
                if source.read(4) in magic:native.append(path)
    if native!=[executable]:raise ValueError('unexpected nested Mach-O code: '+str(native))
    evidence=a.output.parent/(a.output.name+'-evidence');evidence.mkdir()
    snapshot=dict(product=a.product,product_revision=revision,tools_revision=clean_revision(ROOT),
                  input_guest_revision=guest['source_revision'],input_compiled_guest_revision=guest['build_environment'].get('compiled_guest_source_revision',guest['source_revision']),
                  input_provenance_sha256=sha(resources/'provenance.json'),seed_sha256=sha(resources/'System.img'),
                  input_launcher_sha256=sha(executable),team_id=a.team_id,
                  signing_identity=a.identity,distribution='public-preview' if a.public_preview else 'internal-development',
                  app_version=version,release_tag=a.release_tag,
                  input_source_url=source_lock['CORE_URL'],input_source_branch=source_lock['CORE_BRANCH'],
                  source_access='Corresponding AROS source: '+source_lock['CORE_URL']+' at '+source_lock['CORE_REVISION']+'. Recipients need access to this exact source and applicable component sources before redistribution.',
                  external_qemu_required=a.product=='qemu',
                  notice_files={str(path.relative_to(resources)):sha(path) for path in sorted((resources/'Licenses').rglob('*')) if path.is_file()})
    write_json(app/'Contents/Resources/DISTRIBUTION.json',snapshot)
    entitlements=a.repo/'distribution-entitlements.plist'
    run(['codesign','--force','--sign',a.identity,'--options','runtime','--timestamp','--entitlements',entitlements,app],evidence/'codesign-sign.log')
    verify_app(app,a.team_id,evidence,entitlements)
    run(['otool','-L',executable],evidence/'launcher-dependencies.log')
    snapshot.update(app_name=app.name,launcher_sha256=sha(executable),app_bundle_identifier=info['CFBundleIdentifier'])
    write_json(a.output/'distribution-provenance.json',snapshot)
    (a.output/'guest-source.lock').write_text('# Actual immutable packaged guest source; not a retag to current development HEAD.\n'+'\n'.join(key+'='+value for key,value in source_lock.items())+'\n')
    (a.output/'Applications').symlink_to('/Applications')
    prerequisite=('No QEMU or external hypervisor application is required. Apple Silicon and macOS 13 or later are required.' if a.product=='vm' else
        'Install QEMU 11 separately (for example: brew install qemu). The launcher uses /opt/homebrew/bin/qemu-system-aarch64. Apple Silicon macOS is required. QEMU is NOT bundled. Safe guest flush/quit is NOT implemented; window close can interrupt cached writes. This development build is not qualified for personal persistent data. Host clipboard/file bridge and audio are unavailable.')
    release_notice=('Public development preview '+a.release_tag+'. Corresponding source archives and component materials accompany this release. Use the exact release assets and revisions recorded below; this preview is not a production-data release.' if a.public_preview else 'Internal artifact for John Knipper. No public release or redistribution is authorized here.')
    text=f'''PomAROS {'VM' if a.product=='vm' else 'QEMU'} {version} — {'public development preview' if a.public_preview else 'internal development distribution'}

Drag the application to Applications, then open it. It starts in a normal window.
{prerequisite}

Existing personal profiles are never overwritten by installing this application.
The app creates its own persistent profile under ~/Library/Application Support/.

{release_notice}
PomAROS product launcher/tool sources are public at https://github.com/pomaros.
Corresponding AROS source: {source_lock['CORE_URL']}
Exact source revision: {source_lock['CORE_REVISION']}
Recipients must receive access to that exact core and applicable component sources
before external redistribution. A newer public source pin does not relabel an older
private guest payload or supply its missing corresponding source.
The immutable guest payload retains its actual source identity in provenance.json;
distribution signing does not relabel older compiled guest code as a newer revision.

Host launcher/tool code: MIT, John Knipper. Guest AROS and its components include
AROS Public License, GPL, LGPL and additional component terms; full texts and
notices are in Contents/Resources/AROS-* and the bundled component notices.
This software is based in part on the work of the FreeType Team.
This software is based in part on the work of the Independent JPEG Group.
Unicode, compiler-rt and external component notices are bundled in Licenses/.
libfdt: BSD/GPL choice,
notices in Contents/Resources/Licenses/libfdt. QEMU firmware licenses, when applicable,
are in Contents/Resources/edk2-licenses.txt. QEMU itself remains an external dependency.
See the app's bundled notices for full terms; distribution-provenance.json records inputs.
'''
    (a.output/'READ ME.txt').write_text(text)
    print(json.dumps({'result':'PASS','app':str(app),'evidence':str(evidence),'launcher_sha256':snapshot['launcher_sha256']}))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['prepare','zip','dmg','submit','info','staple'])
    p.add_argument('--product',choices=['vm','qemu'],default='vm');p.add_argument('--repo',type=Path,default=ROOT)
    p.add_argument('--public-preview',action='store_true',help='authorized preview with public matching source and release assets')
    p.add_argument('--release-tag',help='tag for the associated public preview source assets')
    p.add_argument('--guest-source-lock',type=Path,help='actual original core lock for a retained historical guest payload')
    p.add_argument('--app',type=Path);p.add_argument('--distribution',type=Path);p.add_argument('--output',type=Path)
    p.add_argument('--identity',default=IDENTITY);p.add_argument('--team-id',default='C43N3NG7Z5')
    p.add_argument('--acceptance',type=Path);p.add_argument('--file',type=Path);p.add_argument('--profile');p.add_argument('--id');p.add_argument('--evidence',type=Path)
    a=p.parse_args();a.repo=a.repo.resolve()
    if a.output:a.output=a.output.resolve()
    if a.action=='prepare':
        if not a.output:p.error('prepare requires --output')
        prepare(a);return
    if a.action in ('zip','dmg'):
        if not a.distribution or not a.output or a.output.exists():p.error('archive requires --distribution and NEW --output')
        metadata=json.loads((a.distribution/'distribution-provenance.json').read_text());app=a.distribution/metadata['app_name']
        if not a.acceptance:p.error('archive requires --acceptance from an exact signed windowed run')
        acceptance=json.loads(a.acceptance.read_text())
        executable=app/'Contents/MacOS'/plistlib.loads((app/'Contents/Info.plist').read_bytes())['CFBundleExecutable']
        if acceptance.get('result')!='PASS' or acceptance.get('launcher_sha256')!=sha(executable):raise ValueError('runtime acceptance does not match this exact signed executable')
        shutil.copyfile(a.acceptance,a.distribution/'Runtime-acceptance.json')
        evidence=a.output.parent/(a.output.stem+'-archive-evidence');evidence.mkdir(exist_ok=True)
        verify_app(app,a.team_id,evidence,a.repo/'distribution-entitlements.plist')
        if a.action=='zip':run(['ditto','-c','-k','--keepParent','--sequesterRsrc',app,a.output],evidence/'zip.log')
        else:
            run(['xcrun','stapler','validate',app],evidence/'nested-app-ticket.log')
            run(['hdiutil','create','-ov','-format','UDZO','-volname','PomAROS '+('VM' if a.product=='vm' else 'QEMU'),'-srcfolder',a.distribution,a.output],evidence/'hdiutil-create.log')
            run(['codesign','--force','--sign',a.identity,'--timestamp',a.output],evidence/'dmg-sign.log')
            run(['codesign','--verify','--strict','--verbose=2',a.output],evidence/'dmg-sign-verify.log')
            run(['hdiutil','verify',a.output],evidence/'hdiutil-verify.log')
        write_json(evidence/'archive.json',{'file':a.output.name,'sha256':sha(a.output),'bytes':a.output.stat().st_size,'launcher_sha256':sha(executable)})
        print(json.dumps({'result':'PASS','file':str(a.output),'sha256':sha(a.output)}));return
    if not a.evidence:p.error('notarization actions require --evidence directory')
    a.evidence.mkdir(parents=True,exist_ok=True)
    if a.action=='submit':
        if not a.file or not a.profile:p.error('submit requires --file and --profile')
        stdout,_=run(['xcrun','notarytool','submit',a.file,'--keychain-profile',a.profile,'--output-format','json'],a.evidence/'submit.log')
        result=json.loads(stdout);result.update(submitted_sha256=sha(a.file),submitted_file=str(a.file.resolve()));write_json(a.evidence/'submission.json',result);print(json.dumps(result))
    elif a.action=='info':
        if not a.id or not a.profile:p.error('info requires --id and --profile')
        stdout,_=run(['xcrun','notarytool','info',a.id,'--keychain-profile',a.profile,'--output-format','json'],a.evidence/'info.log');write_json(a.evidence/'info.json',json.loads(stdout));print(stdout)
    else:
        if not a.file:p.error('staple requires --file')
        run(['xcrun','stapler','staple',a.file],a.evidence/'staple.log');run(['xcrun','stapler','validate',a.file],a.evidence/'validate.log')
        if a.file.suffix=='.app':run(['spctl','--assess','--type','execute','--verbose=2',a.file],a.evidence/'gatekeeper.log')
        else:run(['spctl','--assess','--type','open','--context','context:primary-signature','--verbose=2',a.file],a.evidence/'gatekeeper.log')
        print(json.dumps({'result':'PASS','stapled':str(a.file)}))

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError) as error:
        print('distribution result=FAIL reason='+str(error),file=sys.stderr);raise SystemExit(1)
