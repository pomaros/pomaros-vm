#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Portable binding subset: accepted trees and build-breaking controls."""
from pathlib import Path
import os
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
    vz = work / 'vz.dtb'
    subprocess.run(['make', '-s', 'build/vz-platform-test'], cwd=root, check=True)
    subprocess.run([str(root / 'build/vz-platform-test')], cwd=root, check=True,
                   capture_output=True,
                   env={**os.environ, 'AROS_VZ_FDT_OUTPUT': str(vz)})
    assert 'bound=4 errors=0' in check(vz, 0)
    for label, node, prop, words, expected in (
            ('bad-reg', '/pcie', 'reg', ['1', '2', '3'], 'invalid reg encoding'),
            ('bad-ranges', '/pcie', 'ranges', ['1'], 'invalid ranges-required encoding'),
            ('bad-msi-parent', '/pcie', 'msi-parent', ['dead'],
             'no enabled msi-controller supplier')):
        bad = work / (label + '.dtb'); bad.write_bytes(vz.read_bytes())
        subprocess.run(['fdtput', '-t', 'x', str(bad), node, prop, *words], check=True)
        assert expected in check(bad, 1), label
print('[DT1] binding-subset controls=5 result=PASS')
