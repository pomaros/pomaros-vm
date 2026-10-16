#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Acquire the exact source pin without resetting or changing existing worktrees."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def read_lock(path):
    return dict(line.strip().split('=', 1) for line in path.read_text().splitlines()
                if line.strip() and not line.startswith('#'))

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()

def acquire(lock, prefix, destination):
    url, revision = lock.get(prefix+'_URL'), lock.get(prefix+'_REVISION', '')
    if not url:
        raise ValueError('dependency lock requires a source URL')
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('dependency lock requires an exact 40-digit Git revision')
    destination = destination.absolute()
    if destination.is_symlink():
        raise ValueError('dependency destination cannot be a symlink')
    if destination.exists():
        if Path(git(destination, 'rev-parse', '--show-toplevel')).resolve() != destination.resolve():
            raise ValueError('dependency destination must be the Git worktree root')
        if git(destination, 'rev-parse', 'HEAD') != revision or git(destination, 'status', '--porcelain'):
            raise ValueError(str(destination)+' must already be clean at the exact pin; choose a NEW path instead of overwriting it')
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Git refuses an occupied destination. Failed clones remain visible for
        # diagnosis; this command never resets or deletes an existing checkout.
        subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout', '--', url, str(destination)], check=True)
        subprocess.run(['git', '-C', str(destination), 'checkout', '--detach', revision], check=True)
        if git(destination, 'rev-parse', 'HEAD') != revision or git(destination, 'status', '--porcelain'):
            raise ValueError('new dependency checkout does not match the requested pin')
    print(prefix.lower()+' source result=PASS revision='+revision+' path='+str(destination))
    return destination

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=ROOT.parent/'aros-core')
    p.add_argument('--core-lock', type=Path, default=ROOT/'CORE.lock')
    a = p.parse_args()
    try:
        acquire(read_lock(a.core_lock), 'CORE', a.source)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        p.error(str(error))

if __name__ == '__main__':
    main()
