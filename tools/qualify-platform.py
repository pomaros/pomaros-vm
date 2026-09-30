#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reproduce portable discovery, timer delivery and failure controls."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
p.add_argument('--rpi4-dtb',type=Path,default=root.parent/'aros-raspi/payloads/pi4/bcm2711-rpi-4-b.dtb')
a=p.parse_args()
if a.output.exists():p.error('output must be new')
if not a.rpi4_dtb.is_file():p.error('supply the Pi4 firmware tree with --rpi4-dtb')
a.output.mkdir(parents=True);out=a.output.resolve()
def run(args):subprocess.run(args,cwd=root,check=True)
result={'result':'FAIL','scope':'portable-bootstrap-probes','physical_hardware':False,
        'rpi4_firmware_dtb_sha256':hashlib.sha256(a.rpi4_dtb.read_bytes()).hexdigest()}
try:
 run(['python3','tools/build-fdt-probe.py'])
 run(['python3','tools/run-fdt-probe.py','--output',str(out/'virt-hvf')])
 pi=out/'rpi4-pl011.dtb';shutil.copyfile(a.rpi4_dtb,pi)
 # QEMU's modeled PL011 is the probe console. Record this override; it does
 # not establish the firmware's default console policy on a physical Pi.
 run(['fdtput','-t','s',str(pi),'/chosen','stdout-path','/soc/serial@7e201000:115200n8'])
 run(['fdtput','-t','s',str(pi),'/soc/serial@7e201000','status','okay'])
 result['rpi4_console_override']='/soc/serial@7e201000:115200n8'
 run(['python3','tools/run-fdt-probe.py','--platform','rpi4','--accel','tcg','--dtb',str(pi),'--output',str(out/'rpi4-tcg')])
 run(['python3','tests/interrupt-controls.py','--positive',str(out/'virt-hvf'),'--output',str(out/'controls')])
 result['result']='PASS'
finally:
 (out/'verdict.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
