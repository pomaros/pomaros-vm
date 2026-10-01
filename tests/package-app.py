#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Host-only release admission: incomplete/probe/commented payloads cannot ship."""
from pathlib import Path
import runpy
import unittest

module = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/package-app.py'))
validate = module['validate_desktop']

class Admission(unittest.TestCase):
    def setUp(self):
        self.manifest = dict(profile='wanderer', desktop_session='forever',
                             **{feature: True for feature in module['DESKTOP_FEATURES']})
        self.startup = 'Run >NIL: SYS:C/VMSync VOLUME=VMWORK0:\nG4Ready\nEndCLI\n'

    def test_normal(self):
        validate(self.manifest, self.startup)
        validate(self.manifest, self.startup.replace('VMWORK0:', 'VMWORK0: ; flush'))

    def test_missing_feature(self):
        for feature in module['DESKTOP_FEATURES']:
            with self.subTest(feature=feature):
                candidate = dict(self.manifest); candidate.pop(feature)
                with self.assertRaises(ValueError): validate(candidate, self.startup)

    def test_probe(self):
        for probe in module['QUALIFICATION_PROBES']:
            with self.subTest(probe=probe):
                with self.assertRaises(ValueError): validate(dict(self.manifest, **{probe: True}), self.startup)

    def test_shutdown_must_execute(self):
        for startup in ['', '; '+self.startup, '  ; '+self.startup,
                        'Echo "SYS:C/VMSync VOLUME=VMWORK0:"\n',
                        self.startup.replace('VMWORK0:', 'OTHER:')]:
            with self.subTest(startup=startup):
                with self.assertRaises(ValueError): validate(self.manifest, startup)

    def test_not_a_live_desktop(self):
        for override in [dict(profile='headless'), dict(desktop_session='20')]:
            with self.assertRaises(ValueError): validate(dict(self.manifest, **override), self.startup)

if __name__ == '__main__': unittest.main()
