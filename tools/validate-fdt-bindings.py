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


def parent(node):
    return node.rsplit('/', 1)[0] or '/'


def cell_count(tree, node, name, default):
    result = subprocess.run(['fdtget', '-t', 'x', str(tree), node, name],
                            capture_output=True, text=True)
    if result.returncode:
        return default
    values = result.stdout.split()
    if len(values) != 1:
        raise ValueError(f'{node}: invalid {name}')
    value = int(values[0], 16)
    if value > 4:
        raise ValueError(f'{node}: unsupported {name}={value}')
    return value


def tuple_width(tree, node, kind):
    if kind == 'reg':
        bus = parent(node)
        return cell_count(tree, bus, '#address-cells', 2) + cell_count(tree, bus, '#size-cells', 1)
    return (cell_count(tree, node, '#address-cells', 2) +
            cell_count(tree, parent(node), '#address-cells', 2) +
            cell_count(tree, node, '#size-cells', 1))


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
    paths = list(nodes(tree))
    properties_by_node = {
        node: set(subprocess.check_output(['fdtget', '-p', str(tree), node], text=True).splitlines())
        for node in paths
    }
    phandles = {}
    for node, properties in properties_by_node.items():
        for key in ('phandle', 'linux,phandle'):
            if key not in properties:
                continue
            raw = property_bytes(tree, node, key)
            value = int.from_bytes(raw, 'big') if len(raw) == 4 else 0
            if not value or value == 0xffffffff:
                errors.append(f'{node}: invalid {key}')
            elif value in phandles and phandles[value] != node:
                errors.append(f'{node}: duplicate {key}={value}')
            else:
                phandles[value] = node
    for node in paths:
        properties = properties_by_node[node]
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
                     'two-cells': lambda: len(data) == 8,
                     'cells': lambda: bool(data) and len(data) % 4 == 0,
                     'cells-or-empty': lambda: len(data) % 4 == 0,
                     'strings': lambda: strings(data) is not None,
                     'reg': lambda: bool(data) and tuple_width(tree, node, 'reg') > 0 and
                         len(data) % (4 * tuple_width(tree, node, 'reg')) == 0,
                     'ranges': lambda: len(data) == 0 or tuple_width(tree, node, 'ranges') > 0 and
                         len(data) % (4 * tuple_width(tree, node, 'ranges')) == 0,
                     'ranges-required': lambda: bool(data) and tuple_width(tree, node, 'ranges') > 0 and
                         len(data) % (4 * tuple_width(tree, node, 'ranges')) == 0}[kind]()
            if not valid:
                errors.append(f'{node}: {name} has invalid {kind} encoding')
        for key, marker in (('interrupt-parent', 'interrupt-controller'),
                            ('msi-parent', 'msi-controller')):
            if key not in properties:
                continue
            raw = property_bytes(tree, node, key)
            if len(raw) != 4:
                continue  # The u32 type error was reported above.
            supplier = phandles.get(int.from_bytes(raw, 'big'))
            if (supplier is None or marker not in properties_by_node[supplier] or
                    not enabled(tree, supplier)):
                errors.append(f'{node}: {key} has no enabled {marker} supplier')
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
