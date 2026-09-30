#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Prove real timer.device waits depend on the virtual timer PPI binding."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--esp', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--negative-esp', type=Path, required=True)
a = p.parse_args()
if a.output.exists(): p.error('output must not exist')
a.output.mkdir(parents=True)
a.output = a.output.resolve()
# Build the negative with only the enabled PPI changed (timer_id ^ 2U),
# keeping timer_id for acknowledgement unchanged; firmware FDT stays intact.
# Both bundles are isolated, immutable inputs and retained by run-qemu.
for name,esp in [('positive',a.esp),('negative',a.negative_esp)]:
    run = subprocess.run([sys.executable,str(root/'tools/run-qemu.py'),'--esp',str(esp),
        '--output',str(a.output/name),'--require-timer','--timeout','20'])
    verdict=json.loads((a.output/name/'verdict.json').read_text())
    log=(a.output/name/'serial.log').read_text()
    if name=='positive':
        assert run.returncode==0 and verdict['result']=='PASS',verdict
    else:
        assert run.returncode==1 and verdict['reason']=='timeout',verdict
        assert '[VM] GIC + CNTV deadline ready' in log
        assert '[VM] timer.device three waits PASS' not in log
        assert (a.output/name/'registers.txt').stat().st_size>0
summary={'result':'PASS','scope':'real-kernel-timer-device-PPI-negative-control',
    'bundle_sha256':hashlib.sha256((a.esp/'AROS/GEN0.BND').read_bytes()).hexdigest(),
    'negative_bundle_sha256':hashlib.sha256((a.negative_esp/'AROS/GEN0.BND').read_bytes()).hexdigest(),
    'mutation':'AROSGICEnable uses timer_id ^ 2U; firmware FDT unchanged'}
(a.output/'verdict.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary))
