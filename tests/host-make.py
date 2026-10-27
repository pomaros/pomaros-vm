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

# Exercise the real build-core entry point against a Makefile with AROS's
# .DEFAULT dispatch boundary. Requested roots must reach one MetaMake call
# after a single shared host bootstrap, with all temporary makefiles removed.
import importlib.util
import json
from unittest.mock import patch
spec = importlib.util.spec_from_file_location('vm_build_core',
    Path(__file__).resolve().parents[1] / 'tools/build-core.py')
build_core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_core)
with tempfile.TemporaryDirectory(prefix='vm-core-graph-') as temporary:
    root = Path(temporary); source = root/'source'; source.mkdir()
    subprocess.run(['git','init','-q',str(source)],check=True)
    subprocess.run(['git','-C',str(source),'-c','user.name=Build Test',
        '-c','user.email=build@example.invalid','commit','--allow-empty','-qm','fixture'],check=True)
    build=root/'build';build.mkdir()
    (build/'mmake.py').write_text('import json,sys\nfrom pathlib import Path\n'
        'Path("targets.json").write_text(json.dumps(sys.argv[1:]))\n'
        'assert Path("bootstrap").read_text()=="tools\\nfeatures\\n"\n')
    (build/'Makefile').write_text('.PHONY: crosstools-toolchain features\n'
        'MMAKE := '+sys.executable+' mmake.py\n'
        'crosstools-toolchain:\n\t@echo tools >>bootstrap\n'
        'features: crosstools-toolchain\n\t@echo features >>bootstrap\n'
        '.DEFAULT:\n\t@echo unsafe independent bootstrap >&2\n\t@exit 71\n')
    with patch.object(build_core,'config',return_value=(None,source)), \
         patch.object(sys,'argv',['build-core.py','--build',str(build),'--jobs','8','alpha','beta']):
        build_core.main()
    assert json.loads((build/'targets.json').read_text())==['AROS.alpha','AROS.beta']
    assert not list(build.glob('.core-targets-*')) and not list(build.glob('.host-make-*'))
    (build/'bootstrap').unlink();(build/'targets.json').unlink()
    # A failed MetaMake graph must propagate failure and release temporary state.
    (build/'mmake.py').write_text('raise SystemExit(19)\n')
    with patch.object(build_core,'config',return_value=(None,source)), \
         patch.object(sys,'argv',['build-core.py','--build',str(build),'alpha','beta']):
        try:
            build_core.main()
            raise AssertionError('failed graph accepted')
        except SystemExit as error:
            assert error.code==1
    assert not list(build.glob('.core-targets-*')) and not list(build.glob('.host-make-*'))
print('core-graph result=PASS one-bootstrap one-MetaMake-root-set failure-and-temporary-cleanup')
