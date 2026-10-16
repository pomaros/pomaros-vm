#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise the real allocator's bounds using host-only Exec exclusion stubs."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
core = Path(os.environ.get('AROS_CORE_SOURCE', root.parent / 'aros-core-pomaros'))
with tempfile.TemporaryDirectory(prefix='vm-page-arena-') as temporary:
    tmp = Path(temporary)
    headers = {
        'aros/kernel.h': '#define MAP_Executable 4\n#define MAP_Writable 2\n',
        'aros/debug.h': '#define bug(...) ((void)0)\n',
        'exec/execbase.h': '#include <stddef.h>\n',
        'exec/memory.h': '#define MEMF_CLEAR 0x10000\n',
        'proto/exec.h': '#include <assert.h>\nextern int exclusion_depth;\nstatic inline void Forbid(void) { ++exclusion_depth; }\nstatic inline void Permit(void) { assert(exclusion_depth > 0); --exclusion_depth; }\n',
    }
    for name, content in headers.items():
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    for vm in (0, 1):
        executable = tmp / ('test-' + str(vm))
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                        '-Werror', '-Wno-unused-variable', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-DAROS_VM_GUEST=' + str(vm),
                        '-I' + str(tmp), '-I' + str(core / 'arch/aarch64-apple/include'),
                        '-I' + str(core / 'arch/aarch64-apple/kernel'),
                        str(root / 'tests/page-arena.c'),
                        str(core / 'arch/aarch64-apple/kernel/page_allocator.c'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print('page-arena bounds PASS mode=' + ('VM' if vm else 'native'))
