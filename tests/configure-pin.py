#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Wrong source/ABI pins fail before a configured build directory is created."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import venv

ROOT = Path(__file__).resolve().parents[1]

class Pin(unittest.TestCase):
    def test_selected_venv_keeps_its_import_context(self):
        with tempfile.TemporaryDirectory(prefix='pomaros-python-') as directory:
            root = Path(directory)
            environment = root / 'venv'
            venv.EnvBuilder(with_pip=False, symlinks=True).create(environment)
            python = environment / 'bin/python'
            purelib = Path(subprocess.check_output([str(python), '-c',
                'import sysconfig; print(sysconfig.get_path("purelib"))'], text=True).strip())
            marker = root / 'venv-selected'
            (purelib / 'mako.py').write_text(
                'from pathlib import Path\nPath(' + repr(str(marker)) + ').touch()\n')
            (purelib / 'yaml.py').write_text('# Import-context fixture\n')
            source = root / 'core'
            (source / 'scripts').mkdir(parents=True)
            (source / 'scripts/build-date.py').write_text('pass\n')
            def git(*args):
                return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()
            git('init', '-q', '-b', 'main')
            git('add', '.')
            git('-c', 'user.name=Pin Test', '-c', 'user.email=pin@example.invalid',
                'commit', '-qm', 'fixture')
            lock = root / 'CORE.lock'
            lock.write_text('CORE_REVISION=' + git('rev-parse', 'HEAD') +
                '\nCORE_TARGET=apple-aarch64\nCORE_X18_POLICY=unrestricted\n')
            toolchain = root / 'toolchain'
            toolchain.mkdir()
            output = root / 'build'
            result = subprocess.run([sys.executable, str(ROOT / 'tools/configure-core.py'),
                '--source', str(source), '--output', str(output), '--core-lock', str(lock),
                '--toolchain', str(toolchain), '--python', str(python)],
                capture_output=True, text=True, timeout=30)
            # Stop at the intentionally absent catalog submodules. The real
            # interpreter probe must have imported this venv's module first,
            # even if the system Python also happens to provide mako/yaml.
            self.assertTrue(marker.exists(), result.stderr)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('pathspec', result.stderr)
            self.assertFalse(output.exists())

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
