# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Select GNU make 4 and preserve it across hard-coded nested make calls."""
from contextlib import contextmanager
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def select_make(environment):
    command = environment.get('AROS_MAKE') or shutil.which('gmake', path=environment.get('PATH')) or shutil.which('make', path=environment.get('PATH'))
    if not command:
        raise ValueError('GNU make 4 or later is required')
    executable = shutil.which(command, path=environment.get('PATH'))
    if not executable:
        raise ValueError('selected GNU make executable is missing')
    result = subprocess.run([executable, '--version'], capture_output=True,
                            text=True, check=True, timeout=10, env=environment)
    version = re.search(r'GNU Make (\d+)', result.stdout)
    if not version or int(version[1]) < 4:
        raise ValueError('GNU make 4 or later is required; select it with AROS_MAKE')
    return Path(executable).resolve(strict=True)


@contextmanager
def make_environment(directory, environment=None):
    values = dict(os.environ if environment is None else environment)
    executable = select_make(values)
    with tempfile.TemporaryDirectory(prefix='.host-make-', dir=directory) as temporary:
        (Path(temporary) / 'make').symlink_to(executable)
        values.update(MAKE=str(executable),
                      PATH=temporary + os.pathsep + values.get('PATH', ''))
        yield executable, values
