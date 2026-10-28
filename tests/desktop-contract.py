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
from desktop_contract import require_guest_no_crash, require_desktop_residents, cpu_widget_ready_matches, production_desktop_ready, require_nonuniform_guest_viewport

class Desktop(unittest.TestCase):
    def test_shutdown_success_does_not_hide_guest_crash(self):
        clean = '[HOST] guest shutdown requested\n[VMSync] volume quiesced PASS\n[HOST] guest volume quiesced PASS\n'
        require_guest_no_crash(clean)
        for crash in ('[KI] task crashed and suspended: Wanderer task=0x700a6530',
                      '[TRAP] task=Wanderer code=2 esr=0x92000006',
                      '[KI] crash fault: data read through NULL+0x0'):
            with self.subTest(crash=crash), self.assertRaisesRegex(ValueError, 'guest task crash'):
                require_guest_no_crash(clean + crash + '\r\n')

    def test_host_chrome_does_not_qualify_a_blank_guest(self):
        width, top = 1024, 32
        toolbar = bytes(value for x in range(width) for value in (x % 256, 255, 255))
        for color in ((0, 0, 0), (153, 153, 153), (255, 0, 0)):
            rows = [toolbar] * top + [bytes(color) * width] * 768
            with self.subTest(color=color), self.assertRaisesRegex(ValueError, 'guest viewport'):
                require_nonuniform_guest_viewport(width, top + 768, rows)

    def test_invalid_capture_geometry_refused(self):
        for width, height, rows in ((1023, 800, []), (1024, 700, []), (1024, 800, [b''])):
            with self.subTest(width=width, height=height), self.assertRaisesRegex(ValueError, 'geometry'):
                require_nonuniform_guest_viewport(width, height, rows)

    def test_production_readiness(self):
        widget = '[VM] CPU widget ready cores=1 online=1 samples=1\n'
        valid = widget + '[KI] desktop stable at 3508 ms after reset\n[KI] desktop accepted\n[VM] desktop two HID bindings PASS\n'
        self.assertTrue(production_desktop_ready(valid))
        for bad in (valid + widget, valid.replace(widget, '[KI] alive window open\n'),
                    valid.replace('online=1', 'online=0'), valid.replace('samples=1', 'samples=0'),
                    valid.replace('cores=1', 'cores=2'), valid.replace('[KI] desktop accepted\n', ''),
                    valid + '[KI] desktop accepted\n', valid.replace('samples=1', 'samples=1 extra')):
            with self.subTest(text=bad):
                self.assertFalse(production_desktop_ready(bad))
        self.assertTrue(cpu_widget_ready_matches('[VM] CPU widget ready cores=2 online=2 samples=2\n', 2))
        self.assertFalse(cpu_widget_ready_matches(widget, 0))

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
