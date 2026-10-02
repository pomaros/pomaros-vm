#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reject incomplete or contradictory evidence of ordinary-task SMP."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from exec_smp_contract import MARKERS, exec_smp_matches

rows = [f'[SMP] task cpu=0x{cpu:x} phase=0x{phase:x} start=0x{10+phase*30+cpu:x} end=0x{30+phase*30:x} iterations=0x100'
        for phase in range(2) for cpu in range(2)]
valid = '\n'.join((*MARKERS, *rows, '[SMP] AP timer ticks=0xc8',
    '[SMP] AP compute timer ticks phase=0x0 ticks=0x64',
    '[SMP] AP compute timer ticks phase=0x1 ticks=0x64'))
assert exec_smp_matches(valid, 2)
cases = {
    'single CPU': (valid, 1),
    'missing widget': (valid.replace('[KI] alive window open', ''), 2),
    'missing lifecycle marker': (valid.replace(MARKERS[-1], ''), 2),
    'duplicate lifecycle marker': (valid + '\n' + MARKERS[0], 2),
    'missing worker': (valid.replace(rows[0], ''), 2),
    'duplicate worker': (valid + '\n' + rows[0], 2),
    'same CPU': (valid.replace('cpu=0x1', 'cpu=0x0'), 2),
    'unknown CPU': (valid.replace('cpu=0x1', 'cpu=0x2'), 2),
    'no iterations': (valid.replace('iterations=0x100', 'iterations=0x0'), 2),
    'no overlap': (valid.replace('start=0xb', 'start=0x1e'), 2),
    'timer only while idle': (valid.replace('ticks=0x64', 'ticks=0x0'), 2),
    'missing compute timer phase': (valid.replace('[SMP] AP compute timer ticks phase=0x1 ticks=0x64', ''), 2),
    'missing timer': (valid.replace('[SMP] AP timer ticks=0xc8', ''), 2),
    'no timer preemption': (valid.replace('ticks=0xc8', 'ticks=0x1'), 2),
    'duplicate timer': (valid + '\n[SMP] AP timer ticks=0xc8', 2),
    'guest failure': (valid + '\n[SMP] FAIL', 2),
}
for name, (transcript, cpus) in cases.items():
    assert not exec_smp_matches(transcript, cpus), name
print(f'PASS: valid ordinary-task SMP evidence; {len(cases)} negative controls rejected')
