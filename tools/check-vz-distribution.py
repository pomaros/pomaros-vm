#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded signed-production desktop acceptance, optionally with an explicit guest fixture."""
from pathlib import Path
from contextlib import ExitStack
import argparse,hashlib,json,runpy,shutil,subprocess,sys,time
from desktop_contract import production_desktop_ready, require_nonuniform_guest_viewport, require_guest_no_crash, require_fixture_identity
from host_display import keep_display_awake

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--app',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--settle-seconds',type=float,default=2,help='observe after desktop readiness before capture, 0..60 seconds')
parser.add_argument('--memory',type=int,help='guest RAM in MiB; launcher validates platform bounds')
parser.add_argument('--fixture-disk',type=Path,help='isolated production-mode guest fixture; requires --fixture-manifest')
parser.add_argument('--fixture-manifest',type=Path,help='exact baseline/source/disk identity of the guest fixture')
a=parser.parse_args();app=a.app.resolve();out=a.output.resolve()
if out.exists():parser.error('output must be new')
if not 0 <= a.settle_seconds <= 60:parser.error('--settle-seconds requires 0..60')
if a.memory is not None and a.memory <= 0:parser.error('--memory must be positive')
if bool(a.fixture_disk)!=bool(a.fixture_manifest):parser.error('both fixture inputs are required')
subprocess.run(['codesign','--verify','--strict',str(app)],check=True)
seed=app/'Contents/Resources/System.img'
guest=json.loads((app/'Contents/Resources/provenance.json').read_text())['guest']
seed_sha=hashlib.sha256(seed.read_bytes()).hexdigest()
input_disk=a.fixture_disk.resolve() if a.fixture_disk else seed
input_sha=hashlib.sha256(input_disk.read_bytes()).hexdigest()
if a.fixture_manifest:
    fixture=json.loads(a.fixture_manifest.read_text())
    try:require_fixture_identity(fixture,guest['source_revision'],seed_sha,input_sha)
    except ValueError as error:parser.error(str(error))
out.mkdir()
state=out/'state';state.mkdir()
disk=state/'system.img';shutil.copyfile(input_disk,disk)
if hashlib.sha256(disk.read_bytes()).hexdigest()!=input_sha:parser.error('copied guest disk differs from admitted input')
if a.fixture_manifest:(out/'fixture-manifest.json').write_text(json.dumps(fixture,indent=2)+'\n')
launcher=app/'Contents/MacOS/PomAROSVM'
command=[str(launcher),str(disk),str(state),'--headless','--gpu','--gpu-window','--usb-input','--network','--host-services','--clipboard-test','--window-close-test']
if a.memory is not None:command.append('--memory='+str(a.memory))
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
verdict={'result':'FAIL','windowed':True,'guest_probe_injected':bool(a.fixture_disk),'source_revision':guest['source_revision'],
         'seed_sha256':seed_sha,'input_disk_sha256':input_sha,'launcher_sha256':hashlib.sha256(launcher.read_bytes()).hexdigest()}
if a.fixture_manifest:verdict['fixture_manifest_sha256']=hashlib.sha256(a.fixture_manifest.read_bytes()).hexdigest()
verdict['memory_mib']=a.memory if a.memory is not None else 512
verdict['settle_seconds']=a.settle_seconds
process=None;start=time.monotonic();log=out/'console.log';resources=ExitStack()
try:
    with log.open('wb') as output:
        process=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT)
        resources.enter_context(keep_display_awake(process))
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            text=log.read_text(errors='replace')
            if process.poll() is not None or 'FAIL:' in text or '[B2Q] FAIL' in text:raise RuntimeError('guest-exit-or-failure')
            if production_desktop_ready(text):break
            time.sleep(.1)
        else:raise RuntimeError('desktop-timeout')
        deadline=time.monotonic()+a.settle_seconds
        while time.monotonic()<deadline:
            if process.poll() is not None:raise RuntimeError('guest-exit-during-observation')
            text=log.read_text(errors='replace')
            if 'FAIL:' in text or '[B2Q] FAIL' in text:raise RuntimeError('guest-failure-during-observation')
            require_guest_no_crash(text)
            time.sleep(min(.1,max(0,deadline-time.monotonic())))
        (state/'screenshot.request').write_text('capture\n')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if any((state/name).exists() for name in ('screenshot.error','screenshot.done')):break
            time.sleep(.1)
        verdict['host_window_capture']=(state/'screenshot.done').exists()
        if verdict['host_window_capture']:
            shutil.copyfile(state/'desktop-window.png',out/'desktop-window.png')
            frame=runpy.run_path(str(Path(__file__).resolve().parents[1]/'tests/check-aros-render-frame.py'))
            verdict['guest_viewport']=require_nonuniform_guest_viewport(*frame['decode'](out/'desktop-window.png'))
        else:
            error=(state/'screenshot.error').read_text() if (state/'screenshot.error').exists() else 'owned-window-capture-timeout'
            verdict['capture_error']=error
            (out/'host-window-capture-error.txt').write_text(error)
            raise RuntimeError('owned-window-capture-failed: '+error)
        (state/'window-close.request').write_text('close\n')
        assert process.wait(timeout=20)==0
        text=log.read_text(errors='replace')
        require_guest_no_crash(text)
        assert '[HOST] guest volume quiesced PASS' in text
        assert text.count('[HOST] guest shutdown requested')==1
        assert text.count('[VMSync] volume quiesced PASS')==1
        assert '[VM] virtio graphics IRQ completion PASS' in text
        assert hashlib.sha256(seed.read_bytes()).hexdigest()==verdict['seed_sha256']
        assert hashlib.sha256(input_disk.read_bytes()).hexdigest()==input_sha
        verdict.update(result='PASS',reason=('signed-production-launcher-guest-fixture-desktop-graceful-quiesce' if a.fixture_disk else 'real-signed-production-desktop-graceful-quiesce'),seed_unchanged=True,input_disk_unchanged=True)
except Exception as error:verdict['reason']=repr(error)
finally:
    if process and process.poll() is None:
        if verdict['result']!='PASS':
            try:subprocess.run(['/usr/bin/sample',str(process.pid),'1','1','-file',str(out/'host-sample.txt')],capture_output=True,timeout=5)
            except Exception as error:(out/'host-sample-error.txt').write_text(str(error))
        (state/'window-close.request').write_text('close\n')
        try:process.wait(timeout=20)
        except subprocess.TimeoutExpired:process.kill();process.wait();verdict['forced_cleanup']=True
    resources.close()
    verdict['seconds']=round(time.monotonic()-start,2)
    (out/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict));sys.exit(verdict['result']!='PASS')
