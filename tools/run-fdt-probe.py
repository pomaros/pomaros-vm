#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Run the shared early reader on a CPU; compare its actual input with fdtget."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import socket
import subprocess
import time
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--platform',choices=['virt','rpi4'],default='virt')
p.add_argument('--accel',choices=['hvf','tcg'],default='hvf')
p.add_argument('--dtb',type=Path)
p.add_argument('--guest',type=Path)
p.add_argument('--timeout',type=float,default=15)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if a.platform=='rpi4' and (a.accel!='tcg' or a.dtb is None): p.error('rpi4 requires TCG and --dtb')
if a.output.exists(): p.error('output must be new')
a.output.mkdir(parents=True);out=a.output.resolve()
serial=out/'serial.log';qmp=out/'qmp.sock';guest=a.guest.resolve() if a.guest else root/'build/fdt-probe'/f'{a.platform}.bin'
cmd=['qemu-system-aarch64','-M','virt,gic-version=3' if a.platform=='virt' else 'raspi4b','-accel',a.accel]
if a.platform=='virt':cmd+=['-cpu','host' if a.accel=='hvf' else 'cortex-a72','-m','512','-smp','2']
if a.dtb:cmd+=['-dtb',str(a.dtb.resolve())]
cmd+=['-kernel',str(guest),'-display','none','-serial','file:'+str(serial),'-no-reboot',
      '-qmp',f'unix:{qmp},server=on,wait=off']
(out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
verdict={'result':'FAIL','platform':a.platform,'backend':a.accel,'scope':'early-reader-probe',
         'guest_sha256':hashlib.sha256(guest.read_bytes()).hexdigest()}
start=time.monotonic()
with (out/'host.log').open('w') as log:
 proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
 try:
  while time.monotonic()-start<a.timeout:
   text=serial.read_text(errors='replace') if serial.exists() else ''
   if '[DT0] FAIL' in text or '[DT2] FAIL' in text: raise RuntimeError('guest-failure')
   if '[DT2] PASS core-bindings ' in text: break
   if proc.poll() is not None: raise RuntimeError('early-exit')
   time.sleep(.1)
  else: raise RuntimeError('timeout')
  assert text.count('[DT0] PASS reader ')==1
  assert text.count('[DT0] (control) bad-magic refused=1')==1
  irq=re.search(r'\[DT2\] PASS core-bindings gic=(2|3) timer-irq=(\d+) ticks=(\d+) self-ipi=1 idle=wfi',text)
  assert irq and int(irq[3])>100 and 'timer=virtual x18=preserved' in text
  verdict.update(gic=int(irq[1]),timer_irq=int(irq[2]),ticks=int(irq[3]),self_ipi=1)
  match=re.search(r'\[DT0\] tree=(0x[0-9a-f]+) bytes=(\d+)',text);assert match
  address=int(match[1],16);size=int(match[2]);assert 40<=size<=2*1024*1024
  with socket.socket(socket.AF_UNIX) as s:
   s.settimeout(3);s.connect(str(qmp))
   with s.makefile('rwb') as f:
    f.readline()
    def execute(name,args=None):
     request={'execute':name}
     if args:request['arguments']=args
     f.write(json.dumps(request).encode()+b'\n');f.flush()
     while True:
      reply=json.loads(f.readline())
      if 'error' in reply:raise RuntimeError(reply)
      if 'return' in reply:return reply['return']
    execute('qmp_capabilities');execute('stop')
    execute('pmemsave',{'val':address,'size':size,'filename':str(out/'executed.dtb')})
    execute('quit')
  # fdtget -l rejects legal FDT_NOP tags inserted by QEMU raspi4b.
  # dtc validates and normalizes the captured tree before reference queries.
  subprocess.run(['dtc','-q','-I','dtb','-O','dtb','-o',str(out/'reference.dtb'),str(out/'executed.dtb')],check=True,capture_output=True)
  def get(node,*args):return subprocess.run(['fdtget',*args,str(out/'reference.dtb'),node],capture_output=True,text=True,check=True).stdout.strip()
  def prop(node,key,kind='s'):
   r=subprocess.run(['fdtget','-t',kind,str(out/'reference.dtb'),node,key],capture_output=True,text=True)
   return r.stdout.strip() if r.returncode==0 else None
  paths=[];todo=['/'];cpus=0;memory=[];cpuids=[]
  while todo:
   node=todo.pop();paths.append(node);todo += [node.rstrip('/')+'/'+c for c in get(node,'-l').splitlines()]
   kind=prop(node,'device_type')
   if kind not in ('memory','cpu'): continue
   parent=node.rsplit('/',1)[0] or '/'
   ac=int(prop(parent,'#address-cells','x') or '2',16)
   sc=int(prop(parent,'#size-cells','x') or '1',16)
   values=[int(x,16) for x in prop(node,'reg','x').split()]
   def number(c):
    n=0
    for x in c:n=(n<<32)|x
    return n
   for i in range(0,len(values),ac+sc):
    addr=number(values[i:i+ac]);length=number(values[i+ac:i+ac+sc])
    if kind=='memory':memory.append((addr,length))
    else:cpuids.append(addr)
   if kind=='cpu':cpus+=1
  row=re.search(r'PASS reader platform=(\S+) nodes=(\d+) memory=(\d+) cpus=(\d+) stdout=(\S+) root=(\S+)',text)
  assert row and row[1]==a.platform and int(row[2])==len(paths) and int(row[3])==len(memory) and int(row[4])==cpus
  assert sorted((int(x,16),int(y,16)) for x,y in re.findall(r'\[DT0\] memory=(0x[0-9a-f]+)\+(0x[0-9a-f]+)',text))==sorted(memory)
  assert sorted(int(x,16) for x in re.findall(r'\[DT0\] cpu=(0x[0-9a-f]+)',text))==sorted(cpuids)
  assert row[6]==prop('/','compatible').split()[0]
  stdout=prop('/chosen','stdout-path').split(':',1)[0]
  if not stdout.startswith('/'):stdout=prop('/aliases',stdout)
  assert row[5]==stdout
  verdict.update(result='PASS',nodes=len(paths),memory_ranges=len(memory),cpus=cpus,stdout=stdout,
                 dtb_sha256=hashlib.sha256((out/'executed.dtb').read_bytes()).hexdigest(),negative_control='bad-magic-refused')
 except Exception as error:
  verdict['reason']=str(error) or type(error).__name__
  # Capture independent CPU state before terminating a stalled guest.
  try:
   with socket.socket(socket.AF_UNIX) as s:
    s.settimeout(2);s.connect(str(qmp))
    with s.makefile('rwb') as f:
     f.readline()
     for command in [{'execute':'qmp_capabilities'},{'execute':'stop'},
                     {'execute':'human-monitor-command','arguments':{'command-line':'info registers'}}]:
      f.write(json.dumps(command).encode()+b'\n');f.flush()
      while True:
       response=json.loads(f.readline())
       if 'return' in response or 'error' in response:break
     (out/'registers.txt').write_text(str(response)+'\n')
  except Exception as capture: verdict['capture_error']=str(capture)
 finally:
  if proc.poll() is None:
   proc.terminate()
   try:proc.wait(timeout=3)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
verdict['seconds']=round(time.monotonic()-start,2)
(out/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n');print(json.dumps(verdict))
raise SystemExit(0 if verdict['result']=='PASS' else 1)
