#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reject a missing timer IRQ and a vector that fails to restore x18."""
import argparse
import json
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--positive',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);out=a.output.resolve()
verdict=json.loads((a.positive/'verdict.json').read_text())
assert verdict['result']=='PASS' and verdict['platform']=='virt' and verdict['backend']=='hvf'
def run(args,**kwargs):return subprocess.run(args,text=True,capture_output=True,check=True,**kwargs).stdout
# Preserve every other property; only the timer's virtual PPI cell is wrong.
bad=out/'wrong-timer.dtb'
run(['dtc','-q','-I','dtb','-O','dtb','-o',str(bad),str(a.positive/'executed.dtb')])
irq=run(['fdtget','-t','x',str(bad),'/timer','interrupts']).split();assert len(irq)>=9 and int(irq[6],16)==1
irq[7]=format(int(irq[7],16)^1,'x')
run(['fdtput','-t','x',str(bad),'/timer','interrupts',*irq])
r=subprocess.run(['python3',str(root/'tools/run-fdt-probe.py'),'--dtb',str(bad),'--timeout','3','--output',str(out/'wrong-timer')],text=True,capture_output=True)
v=json.loads((out/'wrong-timer/verdict.json').read_text());log=(out/'wrong-timer/serial.log').read_text()
assert r.returncode==1 and v['result']=='FAIL' and v['reason']=='timeout',r.stdout+r.stderr
assert '[DT2] armed ' in log and '[DT2] PASS' not in log and (out/'wrong-timer/registers.txt').is_file()
# Change exactly the x18/x19 restore instruction in the otherwise qualified guest.
syms=run(['/opt/homebrew/opt/llvm/bin/llvm-nm','-n',str(root/'build/fdt-probe/virt.elf')])
address=int(next(line.split()[0] for line in syms.splitlines() if line.endswith(' probe_restore_x18')),16)
offset=address-0x40080000
data=bytearray((root/'build/fdt-probe/virt.bin').read_bytes())
word=int.from_bytes(data[offset:offset+4],'little');assert word&31==18 and (word>>10)&31==19
assert word&0xffc00000==0xa9400000
original=bytes(data);data[offset:offset+4]=(0xd503201f).to_bytes(4,'little')
assert len(data)==len(original) and data[:offset]==original[:offset] and data[offset+4:]==original[offset+4:]
image=out/'bad-x18.bin';image.write_bytes(data)
r=subprocess.run(['python3',str(root/'tools/run-fdt-probe.py'),'--guest',str(image),'--output',str(out/'bad-x18')],text=True,capture_output=True)
v=json.loads((out/'bad-x18/verdict.json').read_text());log=(out/'bad-x18/serial.log').read_text()
assert r.returncode==1 and v['reason']=='guest-failure' and '[DT2] FAIL x18 preservation' in log,r.stdout+r.stderr
result={'result':'PASS','backend':'hvf','controls':['wrong-timer-irq-timeout','x18-restore-omitted'],'scope':'interrupt-probe-negative-controls'}
(out/'verdict.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
