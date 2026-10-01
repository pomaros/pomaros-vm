#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Portable binding subset: accepted tree and two build-breaking controls."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
validator = root / 'tools/validate-fdt-bindings.py'
with tempfile.TemporaryDirectory(prefix='fdt-bindings-') as directory:
    work = Path(directory)
    tree = work / 'portable.dtb'
    subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', str(tree),
                    str(root / 'tests/fdt/portable.dts')], check=True)
    def check(path, expected):
        result = subprocess.run(['python3', str(validator), '--tree', str(path)],
                                capture_output=True, text=True)
        assert result.returncode == expected, (result.stdout, result.stderr)
        return result.stdout
    assert 'errors=0' in check(tree, 0)
    typo = work / 'typo.dtb'; typo.write_bytes(tree.read_bytes())
    subprocess.run(['fdtput', '-t', 'x', str(typo), '/timer', 'interrupt-name', '1'], check=True)
    assert 'unknown property interrupt-name' in check(typo, 1)
    wrong = work / 'wrong.dtb'; wrong.write_bytes(tree.read_bytes())
    subprocess.run(['fdtput', '-t', 'bx', str(wrong), '/timer', 'interrupts', '1'], check=True)
    assert 'invalid cells encoding' in check(wrong, 1)
print('[DT1] binding-subset controls=2 result=PASS')
