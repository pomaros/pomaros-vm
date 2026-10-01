#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Build a new coherent boot + WORK:System disk, retaining profile data."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

TOTAL = 320 * 1024 * 1024
ENV = dict(os.environ, TZ='UTC', LC_ALL='en_US.UTF-8')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def provenance(app):
    resources = app / 'Contents/Resources'
    data = json.loads((resources / 'provenance.json').read_text())
    inventory = data['guest']['distribution_files']
    if not data['guest'].get('distribution') or not isinstance(inventory, dict):
        raise ValueError(f'not a distribution app: {app}')
    for name, entry in inventory.items():
        if (not isinstance(name, str) or not name or any(part in ('', '.', '..') for part in name.split('/'))
                or '\\' in name or ':' in name or not isinstance(entry, dict)
                or not isinstance(entry.get('sha256'), str)
                or not re.fullmatch(r'[0-9a-f]{64}', entry['sha256'])):
            raise ValueError(f'invalid distribution inventory entry: {name!r}')
    seed = resources / 'System.img'
    if seed.stat().st_size != TOTAL or digest(seed) != data['seed_sha256']:
        raise ValueError(f'bad app seed or provenance: {app}')
    return seed, inventory


def run(*args):
    subprocess.run(args, check=True, env=ENV, stdout=subprocess.DEVNULL)


def check_volume(image):
    # Require the distribution marker in boot and WORK:System in the same disk.
    for path in ('::/AROS/DISTRIB.TAG', '::/System'):
        offset = '' if path.startswith('::/AROS') else '@@64M'
        command = ['mdir', '-i', str(image) + offset, path]
        result = subprocess.run(command, env=ENV, capture_output=True)
        if result.returncode:
            raise ValueError(f'not a complete distribution disk: {image}')


def regular_tree(root):
    paths = sorted(root.rglob('*'))
    for path in paths:
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f'unsupported WORK entry: {path}')
    return paths


def upgrade(args):
    old_seed, old_inventory = provenance(args.old_app)
    new_seed, new_inventory = provenance(args.new_app)
    if args.output.exists() or args.output.is_symlink():
        raise ValueError('output already exists')
    if args.disk.resolve() in (args.output.resolve(), old_seed.resolve(), new_seed.resolve()):
        raise ValueError('disk, seed and output must differ')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # VMStorage holds this lock for its full lifetime. The disk lock also
    # excludes other offline tools and prevents a concurrent source mutation.
    lock_path = args.disk.parent / '.launch.lock'
    lock_fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        disk_fd = os.open(args.disk, os.O_RDONLY | os.O_NOFOLLOW)
        try:
            info = os.fstat(disk_fd)
            if not stat.S_ISREG(info.st_mode) or info.st_size != TOTAL:
                raise ValueError('source must be a regular 320 MiB VM disk')
            fcntl.flock(disk_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            check_volume(args.disk)
            with tempfile.TemporaryDirectory(prefix='.vm-upgrade-', dir=args.output.parent) as temporary:
                scratch = Path(temporary)
                old_tree = scratch / 'old'; old_tree.mkdir()
                run('mcopy', '-s', '-i', str(args.disk) + '@@64M', '::*', str(old_tree))
                paths = regular_tree(old_tree)
                # The source disk must correspond to the supplied old package:
                # untouched package files are the anchor for that assertion.
                old_system = old_tree / 'System'
                if not old_system.is_dir():
                    raise ValueError('WORK:System missing')
                matching = sum(1 for rel, meta in old_inventory.items()
                               if (old_system / rel).is_file() and
                               digest(old_system / rel) == meta['sha256'])
                if matching < max(1, len(old_inventory) // 2):
                    raise ValueError('source WORK:System does not match old app provenance')
                output = scratch / 'result.img'
                shutil.copyfile(new_seed, output)
                check_volume(output)
                # FAT names are case-insensitive. Preserve a user file even
                # when the package spells a colliding name differently.
                new_names = {name.casefold() for name in new_inventory}
                archive = 'Upgrade-Saved'
                suffix = 2
                old_top = {path.name.casefold() for path in old_tree.iterdir()}
                while archive.casefold() in old_top:
                    archive = f'Upgrade-Saved-{suffix}'
                    suffix += 1
                changed = []
                copied = []
                for path in paths:
                    if not path.is_file():
                        continue
                    rel = path.relative_to(old_tree).as_posix()
                    if rel.startswith('System/'):
                        system_rel = rel[len('System/'):]
                        previous = old_inventory.get(system_rel)
                        if previous and digest(path) == previous['sha256']:
                            continue  # New package owns this unmodified file.
                        if (system_rel.casefold() in new_names and
                                not system_rel.casefold().startswith('prefs/env-archive/')):
                            rel = archive + '/' + rel
                            changed.append(path.relative_to(old_tree).as_posix())
                    copied.append((path, rel))
                directories = {str(Path(rel).parent) for _, rel in copied}
                # Empty user directories are profile data as well. Keep empty
                # directories under System only when the new package lacks them.
                for path in paths:
                    if not path.is_dir():
                        continue
                    rel = path.relative_to(old_tree).as_posix()
                    if not rel.startswith('System/'):
                        directories.add(rel)
                    elif not (new_inventory and any(name.startswith(rel[7:] + '/')
                                                    for name in new_inventory)):
                        directories.add(rel)
                for directory in list(directories):
                    current = Path(directory)
                    for parent in (current, *current.parents):
                        if str(parent) != '.':
                            directories.add(parent.as_posix())
                # Existing directories are harmless; mmd returns an error for
                # them, so probe first rather than suppressing all failures.
                for directory in sorted(directories, key=lambda p: (p.count('/'), p)):
                    present = subprocess.run(['mdir', '-i', str(output) + '@@64M', '::/' + directory],
                                             env=ENV, capture_output=True)
                    if present.returncode:
                        run('mmd', '-i', str(output) + '@@64M', '::/' + directory)
                for source, rel in copied:
                    run('mcopy', '-o', '-i', str(output) + '@@64M', str(source), '::/' + rel)
                # Validate every migrated byte, not just the mtools exit code.
                for source, rel in copied:
                    actual = subprocess.check_output(['mtype', '-i', str(output) + '@@64M',
                                                      '::/' + rel], env=ENV)
                    if hashlib.sha256(actual).hexdigest() != digest(source):
                        raise ValueError('migration roundtrip mismatch: ' + rel)
                after = os.fstat(disk_fd)
                if (info.st_size, info.st_mtime_ns, info.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                    raise RuntimeError('source disk changed during upgrade')
                with output.open('rb') as stream:
                    os.fsync(stream.fileno())
                os.link(output, args.output)
                directory_fd = os.open(args.output.parent, os.O_RDONLY)
                try: os.fsync(directory_fd)
                finally: os.close(directory_fd)
                return {'result': 'PASS', 'output': str(args.output.resolve()),
                        'preserved_files': len(copied), 'saved_conflicts': changed,
                        'saved_directory': archive if changed else None,
                        'old_disk_unchanged': True}
        finally:
            os.close(disk_fd)
    finally:
        os.close(lock_fd)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--old-app', type=Path, required=True)
    p.add_argument('--new-app', type=Path, required=True)
    p.add_argument('--disk', type=Path, required=True, help='stopped profile system.img')
    p.add_argument('--output', type=Path, required=True, help='new disk; must not exist')
    args = p.parse_args()
    try:
        print(json.dumps(upgrade(args), sort_keys=True))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        p.exit(2, f'upgrade-distribution: {error}\n')


if __name__ == '__main__':
    main()
