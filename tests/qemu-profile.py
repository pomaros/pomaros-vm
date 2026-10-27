#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reject impossible disk/debugger profiles before creating a run or VM."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='qemu-profile-') as temporary:
    root=Path(temporary);esp=root/'esp';esp.mkdir()
    (esp/'provenance.json').write_text(json.dumps(dict(work_volume=True)))
    cases=[([], 'ESP-only runner cannot supply'),
           (['--gdb-wait'], '--gdb-wait requires'),
           (['--gdb','0'], '--gdb requires a port'),
           (['--gdb','1234','--gdb-wait','--supervise'], 'ESP-only runner cannot supply')]
    for i,(arguments,message) in enumerate(cases):
        output=root/str(i)
        result=subprocess.run([sys.executable,str(ROOT/'tools/run-qemu.py'),
            '--esp',str(esp),'--output',str(output),*arguments],capture_output=True,text=True,timeout=10)
        assert result.returncode==2 and message in result.stderr,result.stderr
        assert not output.exists()
print('qemu-profile result=PASS refused=4 no-output-or-target-I/O')

sys.path.insert(0, str(ROOT / 'tools'))
from runtime_context_contract import PASS, runtime_context_matches
assert runtime_context_matches('boot\r\n' + PASS + '\r\nshutdown')
for rejected in ['', PASS + '\n' + PASS, 'prefix ' + PASS,
                 PASS + ' partial', PASS + '\nCRT_ADMISSION FAIL: write',
                 'CRT_ADMISSION PASS: different result',
                 'CRT_ADMISSION PASS: selected cwd and descriptors survive pin instance switch',
                 'CRT_ADMISSION PASS: selected cwd and descriptors survive pin instance switch; RAM/FAT resize verified']:
    assert not runtime_context_matches(rejected), rejected
print('runtime-context-contract result=PASS exact-client-result eight-negative-controls')
