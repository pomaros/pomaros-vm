#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Replay marker admission without changing the original VM verdict."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from vz_contract import expected_markers, markers_match
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
class Options(SimpleNamespace):
    def __getattr__(self,key): return False
options=Options(require_vsock=True,require_pci_hidd=True,require_desktop_session=None)
counts=expected_markers(options);text=a.log.read_text(errors='replace')
assert markers_match(text,counts)
cases=1
for marker in counts:
    assert not markers_match(text.replace(marker,'WITHHELD',1),counts)
    assert not markers_match(text+'\n'+marker,counts)
    cases+=2
assert not markers_match(text+'\n[B2Q] FAIL: injected',counts);cases+=1
result={'result':'PASS','scope':'captured-marker-contract-only','cases':cases,
        'source_log':str(a.log),'source_sha256':hashlib.sha256(a.log.read_bytes()).hexdigest(),
        'expected_counts':counts,'original_vm_verdict_unchanged':True}
with a.output.open('x') as f: json.dump(result,f,indent=2);f.write('\n')
print(json.dumps(result))
