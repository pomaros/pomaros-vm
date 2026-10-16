# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
from pathlib import Path
import argparse
import hashlib,json,os,re,shutil,subprocess,time
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description="Bounded VZ image, forced stream cut and window close qualification")
parser.add_argument('--app',type=Path,default=root/'artifacts/stage6-release/PomAROS VM.app')
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();out=a.output.resolve()
if out.exists():parser.error('output must be new')
out.mkdir(parents=True)
seed=a.app/'Contents/Resources/System.img'
launcher=a.app/'Contents/MacOS/PomAROSVM'
for profile in ('stage6-transport','stage6-image'):
  output=out/profile
  with (out/(profile+'-build.log')).open('w') as log:
    subprocess.run([str(root/'tools/build-abi-probe'),'--profile',profile,str(output)],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
state=out/'state';state.mkdir()
disk=state/'system.img';shutil.copyfile(seed,disk)
offset=str(disk)+'@@67108864'
subprocess.run(['mcopy','-i',offset,'::System/C/IPrefs',str(out/'IPrefs.original')],check=True)
subprocess.run(['mcopy','-i',offset,str(out/'IPrefs.original'),'::System/C/IPrefs.real'],check=True)
subprocess.run(['mcopy','-o','-i',offset,str(out/'stage6-transport'),'::System/C/IPrefs'],check=True)
subprocess.run(['mcopy','-o','-i',offset,str(out/'stage6-image'),'::System/C/Stage6Image'],check=True)
args=[str(launcher),str(disk),str(state),'--headless','--gpu','--gpu-window','--usb-input','--network','--host-services','--clipboard-test','--window-close-test','--transport-cut-test']
verdict=dict(result='FAIL',reason='not-run',seed_sha256=hashlib.sha256(seed.read_bytes()).hexdigest(),launcher_sha256=hashlib.sha256(launcher.read_bytes()).hexdigest(),probe_sha256=hashlib.sha256((out/'stage6-transport').read_bytes()).hexdigest())
start=time.monotonic();log=out/'console.log';proc=None
try:
  with log.open('wb') as output:
    proc=subprocess.Popen(args,stdout=output,stderr=subprocess.STDOUT)
    def until(markers,seconds):
      limit=time.monotonic()+seconds
      while time.monotonic()<limit:
        data=log.read_text(errors='replace')
        if proc.poll() is not None or '[B2Q] FAIL:' in data or 'FAIL: desktop' in data:
          raise RuntimeError('early-exit-or-guest-failure:'+str(proc.poll()))
        if all(x in data for x in markers):return data
        time.sleep(.1)
      raise RuntimeError('timeout:'+repr(markers))
    until(['[STAGE6] ready for forced transport cut'],30)
    (state/'transport-cut.request').write_text('cut\n')
    until(['[HOST] test transport cut active=1','[STAGE6] held session cut PASS','[STAGE6] new host session after cut PASS','[STAGE6] file exchange after cut PASS','[HOST] file service reconnected after cut PASS','[HOST] clipboard service reconnected after cut PASS','[STAGE6] MultiView JPEG window and nonflat pixels PASS','[KI] desktop stable','[VM] desktop two HID bindings PASS','[VM] virtio graphics IRQ completion PASS'],35)
    subprocess.run(['swift',str(root/'tools/capture-window.swift'),str(proc.pid),str(out/'desktop-window.png')],check=True)
    (state/'window-close.request').write_text('close\n')
    assert proc.wait(timeout=20)==0
  data=log.read_text(errors='replace')
  assert data.count('[HOST] test window close dispatched')==1
  assert data.count('[HOST] guest shutdown requested')==1
  assert data.count('[VMSync] volume quiesced PASS')==1
  assert data.count('[HOST] guest volume quiesced PASS')==1
  expected=b'stage6-forced-cut-fixture-20261001\n'
  assert (state/'Shared/stage6-cut-host.txt').read_bytes()==expected
  for name in ('source','copy'):
    got=subprocess.check_output(['mtype','-i',offset,'::stage6-cut-'+name+'.txt'])
    assert got==expected,(name,got)
  image_report=subprocess.check_output(['mtype','-i',offset,'::stage6-image.txt'],text=True)
  (out/'stage6-image.txt').write_text(image_report)
  assert 'varied=' in image_report
  assert hashlib.sha256(seed.read_bytes()).hexdigest()==verdict['seed_sha256']
  verdict.update(result='PASS',reason='active-vsock-cut-held-session-failure-guest-reopen-file-put-get-clipboard-reconnect-window-close-graceful-quiesce',seconds=round(time.monotonic()-start,2))
except Exception as error:verdict.update(reason=repr(error),seconds=round(time.monotonic()-start,2))
finally:
  if proc and proc.poll() is None:
    if verdict['result'] != 'PASS':
      verdict['independent_guest_register_api']=False
      try:
        sample=subprocess.run(['/usr/bin/sample',str(proc.pid),'1','1','-file',str(out/'host-sample.txt')],capture_output=True,timeout=5)
        (out/'host-sample.log').write_bytes(sample.stdout+sample.stderr)
      except Exception as error:(out/'host-sample-error.txt').write_text(str(error))
      window=re.search(r'window-id=(\d+)',log.read_text(errors='replace'))
      if window:
        try:
          shot=subprocess.run(['/usr/sbin/screencapture','-x','-o','-l',window[1],str(out/'failure-window.png')],capture_output=True,timeout=5)
          (out/'failure-capture.log').write_bytes(shot.stdout+shot.stderr)
        except Exception as error:(out/'failure-capture-error.txt').write_text(str(error))
    proc.kill();proc.wait();verdict['forced_cleanup']=True
  (out/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict));raise SystemExit(verdict['result']!='PASS')
