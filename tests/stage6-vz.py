# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
from pathlib import Path
import argparse
import hashlib,json,os,re,shutil,struct,subprocess,time
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description="Bounded VZ image, forced stream cut and window close qualification")
parser.add_argument('--app',type=Path,default=root/'artifacts/stage6-release/PomAROS VM.app')
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--core-lock',type=Path,default=root/'CORE.lock',
                    help='explicit source lock for an isolated candidate')
parser.add_argument('--prebuilt-probes',type=Path,help='reuse previously compiled probes with their exact source/toolchain/hash manifest')
a=parser.parse_args();out=a.output.resolve()
if out.exists():parser.error('output must be new')
if os.environ.get('MTOOLS_SKIP_CHECK') not in (None,'0'):parser.error('normal mtools filesystem admission must stay enabled')
out.mkdir(parents=True)
seed=a.app/'Contents/Resources/System.img'
launcher=a.app/'Contents/MacOS/PomAROSVM'
app_identity=json.loads((a.app/'Contents/Resources/provenance.json').read_text())
guest_identity=app_identity['guest']
core_lock=dict(line.split('=',1) for line in a.core_lock.read_text().splitlines() if line and not line.startswith('#'))
if guest_identity['source_revision']!=core_lock['CORE_REVISION']:parser.error('app core differs from CORE.lock')
sdk=Path(os.environ.get('AROS_VM_SDK',root.parent/'Build/pomaros-vm/reference-core/bin/apple-aarch64')).resolve()
build=sdk.parents[1]
build_identity=json.loads((build/'vm-build-environment.json').read_text())
if build_identity['source_revision']!=guest_identity['source_revision']:parser.error('probe SDK differs from app core')
configure=json.loads((build/'vm-configure-command.json').read_text())
prefix='--with-aros-toolchain-install='
installations=[argument[len(prefix):] for argument in configure if argument.startswith(prefix)]
if len(installations)!=1:parser.error('SDK configure must identify one guest toolchain')
toolchain=Path(installations[0]).resolve()
source=Path(configure[0]).parent
probe_environment=dict(os.environ,AROS_CORE_SOURCE=str(source))
profiles=('stage6-transport','stage6-image')
clang_sha=hashlib.sha256((toolchain/'bin/clang').read_bytes()).hexdigest()
if a.prebuilt_probes:
  probes=json.loads((a.prebuilt_probes/'probes.json').read_text())
  if probes['source_revision']!=guest_identity['source_revision'] or probes['toolchain_clang_sha256']!=clang_sha:parser.error('prebuilt probe source/toolchain differs from app SDK')
  for profile in profiles:
    binary=a.prebuilt_probes/profile
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=probes['files'][profile]:parser.error('prebuilt probe hash mismatch')
    shutil.copyfile(binary,out/profile)
else:
  if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=guest_identity['source_revision'] or subprocess.check_output(['git','-C',str(source),'status','--porcelain'],text=True):parser.error('probe source must be clean at the app core revision')
  for profile in profiles:
    with (out/(profile+'-build.log')).open('w') as log:
      subprocess.run([str(root/'tools/build-abi-probe'),'--sdk',str(sdk),'--crosstools',str(toolchain),'--profile',profile,str(out/profile)],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True,env=probe_environment)
  probes={'source_revision':guest_identity['source_revision'],'toolchain_clang_sha256':clang_sha,'files':{profile:hashlib.sha256((out/profile).read_bytes()).hexdigest() for profile in profiles}}
(out/'probes.json').write_text(json.dumps(probes,indent=2)+'\n')
state=out/'state';state.mkdir()
disk=state/'system.img';shutil.copyfile(seed,disk)
offset=str(disk)+'@@67108864'
subprocess.run(['mcopy','-i',offset,'::System/C/IPrefs',str(out/'IPrefs.original')],check=True)
subprocess.run(['mcopy','-i',offset,str(out/'IPrefs.original'),'::System/C/IPrefs.real'],check=True)
subprocess.run(['mcopy','-o','-i',offset,str(out/'stage6-transport'),'::System/C/IPrefs'],check=True)
subprocess.run(['mcopy','-o','-i',offset,str(out/'stage6-image'),'::System/C/Stage6Image'],check=True)
args=[str(launcher),str(disk),str(state),'--headless','--gpu','--gpu-window','--usb-input','--network','--host-services','--clipboard-test','--window-close-test','--transport-cut-test']
verdict=dict(source_revision=guest_identity['source_revision'],build_environment=build_identity,toolchain=str(toolchain),host_windowed=True,result='FAIL',reason='not-run',seed_sha256=hashlib.sha256(seed.read_bytes()).hexdigest(),launcher_sha256=hashlib.sha256(launcher.read_bytes()).hexdigest(),probe_sha256=hashlib.sha256((out/'stage6-transport').read_bytes()).hexdigest())
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
    (state/'screenshot.request').write_text('capture\n')
    capture_limit=time.monotonic()+10
    while time.monotonic()<capture_limit and not any((state/name).exists() for name in ('screenshot.done','screenshot.error')):time.sleep(.1)
    verdict['host_window_capture']=(state/'screenshot.done').exists()
    if verdict['host_window_capture']:shutil.copyfile(state/'desktop-window.png',out/'desktop-window.png')
    elif (state/'screenshot.error').exists():(out/'host-window-capture-error.txt').write_text((state/'screenshot.error').read_text())
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
  with disk.open('rb') as image:
    image.seek(67108864);bpb=image.read(512)
    sector=struct.unpack_from('<H',bpb,11)[0];reserved=struct.unpack_from('<H',bpb,14)[0]
    fat_sectors=struct.unpack_from('<H',bpb,22)[0]
    assert sector==512 and fat_sectors>0 and bpb[16]==2,'expected two FAT16 copies'
    image.seek(67108864+reserved*sector);fat1=image.read(fat_sectors*sector);fat2=image.read(fat_sectors*sector)
    verdict['fat_copies_equal']=fat1==fat2
    verdict['fat16_clean_shutdown_bit']=bool(struct.unpack_from('<H',fat1,2)[0]&0x8000)
    assert verdict['fat_copies_equal'] and verdict['fat16_clean_shutdown_bit']
  verdict['normal_mtools_admission']=True
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
