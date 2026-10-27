#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Build targets in an existing, source-bound VM SDK with GNU make 4."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from distribution import config
from host_make import make_environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('targets', nargs='+')
    args = parser.parse_args()
    if args.jobs < 1 or any(not re.fullmatch(r'[A-Za-z0-9_-]+', x) for x in args.targets):
        parser.error('positive jobs and explicit make target names are required')
    build = args.build.resolve(strict=True)
    def validate():
        if (build / 'DEVELOPMENT-DELTA.json').exists():
            raise ValueError('mixed development SDK cannot be a reference build')
        _, source = config(build)
        if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True):
            raise ValueError('configured core source is dirty')
        return source
    try:
        source = validate()
        # AROS dispatches unknown top-level goals through separate recursive
        # .DEFAULT recipes. Parallel goals would bootstrap the same host tools
        # concurrently. Submit all requested roots to one MetaMake graph, as
        # the distribution builder does, after one shared bootstrap.
        with tempfile.TemporaryDirectory(prefix='.core-targets-', dir=build) as temporary:
            batch = Path(temporary) / 'targets.mk'
            batch.write_text('.PHONY: pomaros-core-targets\n'
                'pomaros-core-targets: $(CONFIGUREFILES) crosstools-toolchain features\n'
                '\t@+$(CALL) $(MMAKE) $(MMAKE_OPTIONS) ' +
                ' '.join('AROS.' + target for target in args.targets) + '\n')
            with make_environment(build) as (make, environment):
                subprocess.run([str(make), '-C', str(build), '-j'+str(args.jobs),
                    '-f', 'Makefile', '-f', str(batch), 'pomaros-core-targets'],
                    env=environment, check=True)
        if validate() != source:
            raise ValueError('configured source changed during build')
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'build-core: {error}\n')


if __name__ == '__main__':
    main()
