#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Missing shared descriptors must stop real packaging before output creation."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('distribution', ROOT / 'tools/distribution.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Closure(unittest.TestCase):
    def test_fresh_runtime_build_bootstraps_host_tools(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder)
            # Exercise the generated batch with real GNU make. The configured
            # SDK initially has no MetaMake; features must create it before
            # the actual distribution command can run.
            (build / 'mmake-input').write_text(
                '#!/bin/sh\nset -eu\ntest -f toolchain-ready\n'
                'printf "%s\\n" "$@" > built-targets\n')
            (build / 'Makefile').write_text(
                'MMAKE := ./mmake\nCALL := env\n'
                '.PHONY: crosstools-toolchain features\n'
                'crosstools-toolchain:\n\ttouch toolchain-ready\n'
                'features:\n\tcp mmake-input mmake\n\tchmod +x mmake\n')
            args = SimpleNamespace(build=build, jobs=2)
            with patch.object(module, 'config', return_value=(build, build)), \
                 patch.object(module.subprocess, 'check_output', return_value=''), \
                 patch.object(module, 'run'):
                module.build_runtime(args)
            expected = ['AROS.' + name for name in
                        json.loads((ROOT / 'distribution/targets.json').read_text())]
            self.assertEqual((build / 'built-targets').read_text().splitlines(), expected)

    def test_runtime_services_are_required_for_each_backend(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            sdk = root / 'AROS'
            tools = root / 'tools'
            (tools / 'bin').mkdir(parents=True)
            (tools / 'bin/clang').touch()
            build = root / 'build'
            build.mkdir()
            (build / 'vm-configure-command.json').write_text(json.dumps([
                '/source/configure', '--with-aros-toolchain-install=' + str(tools)]))
            services = ['Libs/fd.library', 'Libs/posixc.library',
                        'Libs/stdc.library', 'Libs/stdcio.library']
            for missing in services:
                with self.subTest(missing=missing):
                    for name in module.REQUIRED:
                        p = sdk / name
                        p.parent.mkdir(parents=True, exist_ok=True)
                        p.touch()
                    (sdk / missing).unlink()
                    for backend in ('qemu', 'vz'):
                        output = root / backend
                        args = SimpleNamespace(build=build, output=output, backend=backend)
                        with patch.object(module, 'config', return_value=(sdk, root)), \
                             patch.object(module, 'run', side_effect=AssertionError('packaging ran before closure gate')):
                            with self.assertRaisesRegex(ValueError, 'missing ' + missing):
                                module.package(args)
                        self.assertFalse(output.exists())



if __name__ == '__main__':
    unittest.main()
