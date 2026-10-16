#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise production argument admission without guest products or VM execution."""
import ast
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'tools/package-core.py'
tree = ast.parse(source.read_text(), str(source))
# Run the real admission phase; artifact reads begin at build = a.build.resolve().
stop = next(i for i, node in enumerate(tree.body) if isinstance(node, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == 'build' for t in node.targets))
code = compile(ast.Module(body=tree.body[:stop], type_ignores=[]), str(source), 'exec')

class Backend(unittest.TestCase):
    def admit(self, *extra):
        old = sys.argv
        sys.argv = [str(source), '--build', 'unused', '--efi', 'unused', '--output', 'unused',
                    '--profile', 'wanderer', '--virtio-gpu', '--work-volume',
                    '--usb-desktop', '--network-stack', '--desktop-session', 'forever', *extra]
        try:
            scope = {'__file__': str(source)}
            exec(code, scope)
            return scope['a']
        finally:
            sys.argv = old

    def test_vz_keeps_host_bridge(self):
        a = self.admit()
        self.assertTrue(a.host_services and a.vsock_device and a.virtionet_device)

    def test_qemu_persistent_desktop_without_bridge(self):
        a = self.admit('--backend', 'qemu')
        self.assertFalse(a.host_services or a.vsock_device)
        self.assertTrue(a.work_volume and a.usb_desktop and a.virtionet_device)

    def test_qemu_rejects_unavailable_services(self):
        import contextlib, io
        for flag in ('--host-services', '--vsock-device', '--clipboard-probe', '--shutdown-probe'):
            with self.subTest(flag=flag), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    self.admit('--backend', 'qemu', flag)

if __name__ == '__main__':
    unittest.main()
