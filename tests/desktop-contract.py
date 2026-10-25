#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reject the real missing-resident regression before booting a guest."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from desktop_contract import require_desktop_residents

class Desktop(unittest.TestCase):
    def setUp(self):
        self.source = Path(os.environ.get('AROS_CORE_SOURCE', ROOT.parent / 'aros-core'))
        self.modules = json.loads((ROOT / 'boot/wanderer-modules.json').read_text())

    def test_current_manifest(self):
        require_desktop_residents(self.source, self.modules)

    def test_missing_required_resident(self):
        for name in ('debug.library', 'processor.resource', 'entropy.resource'):
            with self.subTest(name=name):
                candidate = dict(self.modules); candidate.pop(name)
                with self.assertRaisesRegex(ValueError, name.replace('.', r'\.')):
                    require_desktop_residents(self.source, candidate)

    def test_virtio_replacement(self):
        candidate = dict(self.modules); candidate.pop('efifbgfx.hidd')
        candidate['virtiogfx.hidd'] = 'unused'
        require_desktop_residents(self.source, candidate, True)
        with self.assertRaises(ValueError):
            require_desktop_residents(self.source, candidate)

    def test_missing_graphics(self):
        candidate = dict(self.modules); candidate.pop('efifbgfx.hidd')
        with self.assertRaises(ValueError):
            require_desktop_residents(self.source, candidate, True)

    def test_unknown_contract_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            path = source / 'arch/aarch64-apple/kernel/coldstart_proof.c'
            path.parent.mkdir(parents=True); path.write_text('/* no contract */')
            with self.assertRaises(ValueError):
                require_desktop_residents(source, self.modules)

if __name__ == '__main__':
    unittest.main()
