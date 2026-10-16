#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Wrong source/ABI pins fail before a configured build directory is created."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class Pin(unittest.TestCase):
    def test_pin_admission(self):
        with tempfile.TemporaryDirectory(prefix='pomaros-pin-') as directory:
            root = Path(directory); source = root / 'core'; source.mkdir()
            def git(*args):
                return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()
            git('init', '-q', '-b', 'pomaros-common')
            (source / 'source').write_text('source\n')
            git('add', 'source')
            git('-c', 'user.name=Pin Test', '-c', 'user.email=pin@example.invalid', 'commit', '-qm', 'fixture')
            revision = git('rev-parse', 'HEAD')
            # Dependency pins are commonly checked out detached.
            git('checkout', '-q', '--detach', revision)
            for expected, policy, message in [(revision, 'unrestricted', 'cross-toolchain'),
                    ('0' * 40, 'unrestricted', 'differs from CORE.lock'),
                    (revision, 'reserved', 'differs from CORE.lock')]:
                with self.subTest(expected=expected, policy=policy):
                    lock = root / 'CORE.lock'
                    lock.write_text(f'CORE_REVISION={expected}\nCORE_TARGET=apple-aarch64\nCORE_X18_POLICY={policy}\n')
                    output = root / 'build'
                    result = subprocess.run(['python3', str(ROOT / 'tools/configure-core.py'),
                        '--source', str(source), '--output', str(output), '--core-lock', str(lock),
                        '--toolchain', str(root / 'missing')], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                    self.assertFalse(output.exists())

if __name__ == '__main__':
    unittest.main()
