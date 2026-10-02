#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded host contention check of AROS's real AArch64 atomic headers."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core', type=Path, default=root.parent / 'aros-core-smp')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
core = args.core.resolve()
args.output.mkdir(parents=True, exist_ok=True)
report = {'lane': 'AArch64-host-pthreads', 'result': 'FAIL',
          'guest_smp_validated': False, 'core': str(core)}
try:
    if platform.machine() not in ('arm64', 'aarch64'):
        raise RuntimeError('This test requires an AArch64 host')
    report['revision'] = subprocess.check_output(
        ['git', '-C', str(core), 'rev-parse', 'HEAD'], text=True).strip()
    headers = [core / 'compiler/arossupport/include/atomic.h',
               core / 'arch/aarch64-all/include/aros/atomic.h',
               core / 'arch/aarch64-all/include/aros/atomic_v8.h']
    report['headers'] = {str(p.relative_to(core)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in headers}
    with tempfile.TemporaryDirectory() as tmp:
        inc = Path(tmp)
        (inc / 'aros').mkdir()
        (inc / 'aros/aarch64').symlink_to(core / 'arch/aarch64-all/include/aros')
        (inc / 'exec').mkdir()
        # Only the ABI scalar typedefs are substituted; all atomic code and
        # architecture selection come from the checked-out production headers.
        (inc / 'exec/types.h').write_text(
            'typedef signed char BYTE; typedef short WORD; typedef int LONG;\n')
        # The same unmodified headers must also compile for the guest ELF ABI.
        elf_source = inc / 'elf.c'
        elf_source.write_text('#include <atomic.h>\n' + '\n'.join(
            'void ' + op + suffix + '(' + kind + ' *p, unsigned mask) { '
            'AROS_ATOMIC_' + op + '(*p' + (', mask' if op in ('AND', 'OR') else '') + '); }'
            for op in ('INC', 'DEC', 'AND', 'OR')
            for suffix, kind in [('B', 'unsigned char'), ('W', 'unsigned short'), ('L', 'unsigned int')]))
        elf = subprocess.run(['clang', '--target=aarch64-none-elf', '-O2', '-c',
            '-I' + str(inc), '-I' + str(core / 'compiler/arossupport/include'),
            str(elf_source), '-o', str(args.output.resolve() / 'elf.o')],
            capture_output=True, text=True, timeout=30)
        (args.output / 'elf-build.log').write_text(elf.stdout + elf.stderr)
        elf.check_returncode()
        exe = args.output.resolve() / 'smp-atomics'
        cmd = ['clang', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
               '-I' + str(inc), '-I' + str(core / 'compiler/arossupport/include'),
               str(root / 'tests/smp-atomics.c'), '-o', str(exe)]
        report['compile'] = cmd
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        (args.output / 'build.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        report['binary_sha256'] = hashlib.sha256(exe.read_bytes()).hexdigest()
        report['test_sha256'] = hashlib.sha256((root / 'tests/smp-atomics.c').read_bytes()).hexdigest()
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        (args.output / 'run.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        if not result.stdout.startswith('PASS:'):
            raise RuntimeError('Missing completion verdict')
        report['result'] = 'PASS'
except Exception as exc:
    report['error'] = str(exc)
finally:
    (args.output / 'verdict.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
raise SystemExit(0 if report['result'] == 'PASS' else 1)
