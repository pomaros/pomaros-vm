#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Check properties of AROS-bound FDT nodes against portable binding schemas."""
import argparse
import json
from pathlib import Path
import subprocess


def property_bytes(tree, node, name):
    result = subprocess.check_output(['fdtget', '-t', 'bx', str(tree), node, name], text=True)
    return bytes(int(byte, 16) for byte in result.split())


def strings(data):
    if not data or data[-1] or any(not item for item in data[:-1].split(b'\0')):
        return None
    try:
        return [item.decode('ascii') for item in data[:-1].split(b'\0')]
    except UnicodeDecodeError:
        return None


def nodes(tree):
    todo = ['/']
    while todo:
        node = todo.pop()
        yield node
        children = subprocess.check_output(['fdtget', '-l', str(tree), node], text=True).splitlines()
        todo.extend(node.rstrip('/') + '/' + child for child in children)


def enabled(tree, node):
    while True:
        result = subprocess.run(['fdtget', '-t', 's', str(tree), node, 'status'],
                                capture_output=True, text=True)
        if result.returncode == 0 and result.stdout.strip() not in ('ok', 'okay'):
            return False
        if node == '/':
            return True
        node = node.rsplit('/', 1)[0] or '/'


def validate(tree, schemas):
    errors = []
    bound = 0
    for node in nodes(tree):
        properties = set(subprocess.check_output(['fdtget', '-p', str(tree), node], text=True).splitlines())
        if 'compatible' not in properties:
            continue
        compatibles = strings(property_bytes(tree, node, 'compatible'))
        if compatibles is None:
            errors.append(f'{node}: malformed compatible string list')
            continue
        matched = [(name, schema) for name, schema in schemas.items() if name in compatibles]
        if matched and 'status' in properties:
            status = strings(property_bytes(tree, node, 'status'))
            if (status is None or len(status) != 1 or
                    status[0] not in ('ok', 'okay', 'disabled', 'reserved', 'fail') and
                    not status[0].startswith('fail-')):
                errors.append(f'{node}: malformed status')
                continue
        if not enabled(tree, node):
            continue
        if not matched:
            continue  # Unknown hardware remains visible, but is not bound here.
        bound += 1
        # A fallback compatible may have a second binding; both contracts apply.
        allowed = {'compatible', 'status', 'phandle', 'linux,phandle'}
        required = set()
        types = {}
        for _, schema in matched:
            required.update(schema['required'])
            types.update(schema['properties'])
            allowed.update(schema['properties'])
        for name in sorted(required - properties):
            errors.append(f'{node}: missing {name}')
        for name in sorted(properties - allowed):
            errors.append(f'{node}: unknown property {name}')
        for name in sorted(properties & types.keys()):
            data = property_bytes(tree, node, name)
            kind = types[name]
            valid = {'flag': lambda: not data,
                     'u32': lambda: len(data) == 4,
                     'cells': lambda: bool(data) and len(data) % 4 == 0,
                     'cells-or-empty': lambda: len(data) % 4 == 0,
                     'strings': lambda: strings(data) is not None}[kind]()
            if not valid:
                errors.append(f'{node}: {name} has invalid {kind} encoding')
    return bound, errors


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, action='append', required=True)
    p.add_argument('--schemas', type=Path,
                   default=Path(__file__).resolve().parents[1] / 'bindings/fdt-core.json')
    args = p.parse_args()
    schemas = json.loads(args.schemas.read_text())
    total = 0
    errors = []
    for tree in args.tree:
        try:
            if tree.stat().st_size > 2 * 1024 * 1024:
                raise ValueError('tree exceeds the shared early-reader 2 MiB limit')
            count, current = validate(tree, schemas)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            errors.append(f'{tree}: unreadable or invalid tree: {error}')
            continue
        total += count
        errors.extend(f'{tree}: {line}' for line in current)
    for error in errors:
        print(error)
    print(f'[DT1] binding-subset trees={len(args.tree)} bindings={len(schemas)} bound={total} errors={len(errors)}')
    return 1 if errors or not total else 0


if __name__ == '__main__':
    raise SystemExit(main())
