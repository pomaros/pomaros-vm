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
    def test_descriptor_service_is_required_for_each_backend(self):
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
            for name in module.REQUIRED:
                if name != 'Libs/fd.library':
                    p = sdk / name
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.touch()
            for backend in ('qemu', 'vz'):
                output = root / backend
                args = SimpleNamespace(build=build, output=output, backend=backend)
                with patch.object(module, 'config', return_value=(sdk, root)), \
                     patch.object(module, 'run', side_effect=AssertionError('packaging ran before closure gate')):
                    with self.assertRaisesRegex(ValueError, 'missing Libs/fd.library'):
                        module.package(args)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
