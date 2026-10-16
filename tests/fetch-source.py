#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise real Git acquisition, refusal to overwrite, and source provenance."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
fetch=module('fetch_core',ROOT/'tools/fetch-core.py')
distribute=module('distribute',ROOT/'tools/distribute.py')

def git(path,*args):
    return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()

def fixture(path):
    path.mkdir();git(path,'init','-q','-b','main')
    (path/'source').write_text('exact source\n');git(path,'add','source')
    git(path,'-c','user.name=Source Test','-c','user.email=source@example.invalid','commit','-qm','fixture')
    return git(path,'rev-parse','HEAD')

class Source(unittest.TestCase):
    def test_clone_detached_then_refuse_changes_without_overwriting(self):
        with tempfile.TemporaryDirectory(prefix='pomaros source ') as directory:
            root=Path(directory);origin=root/'origin';revision=fixture(origin)
            lock=dict(CORE_URL=str(origin),CORE_REVISION=revision)
            target=root/'selected source'
            with contextlib.redirect_stdout(io.StringIO()):
                fetch.acquire(lock,'CORE',target)
                self.assertEqual(git(target,'rev-parse','HEAD'),revision)
                self.assertEqual(git(target,'branch','--show-current'),'')
                fetch.acquire(lock,'CORE',target)
            with self.assertRaises(ValueError):
                fetch.acquire(dict(CORE_URL=str(origin),CORE_REVISION='b'*40),'CORE',target)
            self.assertEqual(git(target,'rev-parse','HEAD'),revision)
            (target/'source').write_text('personal edit\n')
            with self.assertRaises(ValueError):fetch.acquire(lock,'CORE',target)
            self.assertEqual((target/'source').read_text(),'personal edit\n')
            self.assertEqual(git(target,'rev-parse','HEAD'),revision)
            link=root/'symlink';link.symlink_to(target)
            with self.assertRaises(ValueError):fetch.acquire(lock,'CORE',link)

    def test_invalid_revision_creates_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/'absent'
            with self.assertRaises(ValueError):
                fetch.acquire(dict(CORE_URL='invalid',CORE_REVISION='main'),'CORE',target)
            self.assertFalse(target.exists())

    def test_retained_guest_requires_actual_source_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            lock=Path(directory)/'CORE.lock'
            def write(revision,policy='unrestricted'):
                lock.write_text('CORE_URL=https://example.invalid/original.git\nCORE_BRANCH=main\n'
                    +'CORE_REVISION='+revision+'\nCORE_TARGET=apple-aarch64\nCORE_X18_POLICY='+policy+'\n')
            guest={'source_revision':'a'*40}
            write('b'*40)
            with self.assertRaises(ValueError):distribute.guest_source_lock(lock,guest)
            write('a'*40,'reserved')
            with self.assertRaises(ValueError):distribute.guest_source_lock(lock,guest)
            write('a'*40)
            result=distribute.guest_source_lock(lock,guest)
            self.assertEqual(result['CORE_REVISION'],guest['source_revision'])
            self.assertEqual(result['CORE_URL'],'https://example.invalid/original.git')
            guest['source_lock']=dict(result,CORE_URL='https://example.invalid/different.git')
            with self.assertRaises(ValueError):distribute.guest_source_lock(lock,guest)

if __name__=='__main__':unittest.main()
