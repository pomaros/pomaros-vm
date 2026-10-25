#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Launcher option refusals exit 2 with a message, without a crash or a VM."""
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
launcher = root / 'build/pomaros-vm'
if not launcher.is_file():
    raise SystemExit('build/pomaros-vm is required')
cases = {
    '--memory=18446744073709551615': '--memory requires',  # UInt64 max: must not overflow into a trap
    '--memory=99999999999': '--memory requires',
    '--memory=0': '--memory requires',
    '--memory=abc': '--memory requires',
    '--memory=-1': '--memory requires',
    '--memory=1024 --memory=2048': '--memory requires',
    '--cpus=0': '--cpus requires',
    '--bogus': 'usage:',
}
results = []
for options, expected in cases.items():
    # Nonexistent disk/state: a refusal must happen before any storage access.
    run = subprocess.run([str(launcher), str(root / 'no-such-disk.img'), str(root / 'no-such-state'), *options.split()],
                         capture_output=True, text=True, timeout=30)
    ok = run.returncode == 2 and expected in run.stderr and 'Cannot open' not in run.stderr
    results.append({'options': options, 'returncode': run.returncode, 'stderr': run.stderr.strip(), 'ok': ok})
verdict = {'result': 'PASS' if all(r['ok'] for r in results) else 'FAIL',
           'scope': 'option admission only; no VM started, no disk opened', 'cases': results}
print(json.dumps(verdict, indent=1))
sys.exit(0 if verdict['result'] == 'PASS' else 1)
