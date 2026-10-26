#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise actual GNU make response files behind a misleading named make."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from host_make import make_environment, select_make

real = select_make(dict(os.environ))
with tempfile.TemporaryDirectory(prefix='vm-host-make-') as temporary:
    root = Path(temporary)
    legacy = root / 'legacy'; legacy.mkdir()
    fake = legacy / 'make'
    fake.write_text('#!/bin/sh\nif [ "$1" = --version ]; then echo "GNU Make 3.81"; exit 0; fi\nexit 73\n')
    fake.chmod(0o755)
    env = dict(os.environ, AROS_MAKE=str(real), PATH=str(legacy)+os.pathsep+os.environ['PATH'])
    (root/'Makefile').write_text('all:\n\tmake -f nested.mk\n')
    (root/'nested.mk').write_text('all:\n\t$(file >proof.rsp,verified)\n\t@test -f proof.rsp\n')
    with make_environment(root, env) as (make, selected):
        shim = Path(selected['PATH'].split(os.pathsep)[0])
        assert shim.is_dir()
        subprocess.run([str(make), '--no-print-directory'], cwd=root, env=selected, check=True)
        assert (root/'proof.rsp').read_text().strip() == 'verified'
    assert not shim.exists()
    assert env['PATH'].startswith(str(legacy)+os.pathsep)
    for override in [str(fake), str(root/'missing')]:
        try:
            with make_environment(root, dict(env, AROS_MAKE=override)):
                raise AssertionError('invalid make accepted')
        except ValueError:
            pass
    try:
        with make_environment(root, env) as (_, selected):
            aborted = Path(selected['PATH'].split(os.pathsep)[0])
            raise RuntimeError('injected operation failure')
    except RuntimeError:
        pass
    assert not aborted.exists()
print('host-make result=PASS actual-nested-response-file legacy-and-missing-refused exception-cleanup')
