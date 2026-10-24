#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise production argument admission without guest products or VM execution."""
import ast
import json
import tempfile
from types import SimpleNamespace
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

class WorkSize(unittest.TestCase):
    admit = Backend.admit

    def test_default_and_explicit_sizes(self):
        self.assertEqual(self.admit().work_size, 256)
        self.assertEqual(self.admit('--work-size', '1024').work_size, 1024)

    def test_refuses_unbounded_or_unaligned_sizes(self):
        import contextlib, io
        for size in ('128', '1088', '300', '0', '-256'):
            with self.subTest(size=size), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    self.admit('--work-size', size)

    def test_size_requires_work_volume(self):
        import contextlib, io
        old = sys.argv
        sys.argv = [str(source), '--build', 'unused', '--efi', 'unused', '--output', 'unused',
                    '--profile', 'wanderer', '--virtio-gpu', '--work-size', '512']
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    exec(code, {'__file__': str(source)})
        finally:
            sys.argv = old

class BootBoundary(unittest.TestCase):
    def package_modules(self, root, profile):
        begin = next(i for i, node in enumerate(tree.body) if isinstance(node, ast.Assign)
                     and any(isinstance(t, ast.Name) and t.id == 'modules' for t in node.targets))
        end = next(i for i, node in enumerate(tree.body) if isinstance(node, ast.Assign)
                   and any(isinstance(t, ast.Name) and t.id == 'components' for t in node.targets))
        code = compile(ast.Module(body=tree.body[begin:end], type_ignores=[]), str(source), 'exec')
        import argparse
        scope = dict(root=root, a=SimpleNamespace(profile=profile, virtio_gpu=True),
                     json=json, Path=Path, p=argparse.ArgumentParser())
        exec(code, scope)
        return scope['modules']

    def test_shipped_manifests_keep_vm_devices_without_native_drivers(self):
        for profile in ('headless', 'wanderer'):
            with self.subTest(profile=profile):
                modules = self.package_modules(ROOT, profile)
                self.assertIn('openfirmware.resource', modules)
                self.assertIn('appleboot.device', modules)
                if profile == 'wanderer':
                    self.assertIn('virtiogfx.hidd', modules)
                    self.assertIn('vmpci.hidd', modules)

    def test_reject_native_module_by_name_or_path(self):
        import contextlib, io
        with tempfile.TemporaryDirectory(prefix='pomaros-boot-boundary-') as directory:
            root=Path(directory); (root/'boot').mkdir()
            for manifest in ({'pmgr.resource':'Devs/safe.device'},
                             {'innocent.device':'Devs/applenvme.device'}):
                (root/'boot/wanderer-modules.json').write_text(json.dumps(manifest))
                with self.subTest(manifest=manifest), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        self.package_modules(root, 'wanderer')

if __name__ == '__main__':
    unittest.main()
