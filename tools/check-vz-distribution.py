#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded windowed acceptance of an exact signed VM distribution, with no guest probe."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,time
from desktop_contract import production_desktop_ready, require_nonuniform_guest_viewport

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--app',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();app=a.app.resolve();out=a.output.resolve()
if out.exists():parser.error('output must be new')
subprocess.run(['codesign','--verify','--strict',str(app)],check=True)
out.mkdir()
state=out/'state';state.mkdir();seed=app/'Contents/Resources/System.img'
disk=state/'system.img';shutil.copyfile(seed,disk)
launcher=app/'Contents/MacOS/PomAROSVM';guest=json.loads((app/'Contents/Resources/provenance.json').read_text())['guest']
command=[str(launcher),str(disk),str(state),'--headless','--gpu','--gpu-window','--usb-input','--network','--host-services','--clipboard-test','--window-close-test']
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
verdict={'result':'FAIL','windowed':True,'guest_probe_injected':False,'source_revision':guest['source_revision'],
         'seed_sha256':hashlib.sha256(seed.read_bytes()).hexdigest(),'launcher_sha256':hashlib.sha256(launcher.read_bytes()).hexdigest()}
process=None;start=time.monotonic();log=out/'console.log'
try:
    with log.open('wb') as output:
        process=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            text=log.read_text(errors='replace')
            if process.poll() is not None or 'FAIL:' in text or '[B2Q] FAIL' in text:raise RuntimeError('guest-exit-or-failure')
            if production_desktop_ready(text):break
            time.sleep(.1)
        else:raise RuntimeError('desktop-timeout')
        time.sleep(2)
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
        assert '[HOST] guest volume quiesced PASS' in text
        assert text.count('[HOST] guest shutdown requested')==1
        assert text.count('[VMSync] volume quiesced PASS')==1
        assert '[VM] virtio graphics IRQ completion PASS' in text
        assert hashlib.sha256(seed.read_bytes()).hexdigest()==verdict['seed_sha256']
        verdict.update(result='PASS',reason='real-signed-production-desktop-graceful-quiesce',seed_unchanged=True)
except Exception as error:verdict['reason']=repr(error)
finally:
    if process and process.poll() is None:
        if verdict['result']!='PASS':
            try:subprocess.run(['/usr/bin/sample',str(process.pid),'1','1','-file',str(out/'host-sample.txt')],capture_output=True,timeout=5)
            except Exception as error:(out/'host-sample-error.txt').write_text(str(error))
        (state/'window-close.request').write_text('close\n')
        try:process.wait(timeout=20)
        except subprocess.TimeoutExpired:process.kill();process.wait();verdict['forced_cleanup']=True
    verdict['seconds']=round(time.monotonic()-start,2)
    (out/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict));sys.exit(verdict['result']!='PASS')
