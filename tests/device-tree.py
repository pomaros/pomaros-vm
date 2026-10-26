#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Delegate the DeviceTree regression to its common-core owner."""
import os
from pathlib import Path
import sys

product = Path(__file__).resolve().parents[1]
core = Path(os.environ.get('AROS_CORE_SOURCE', product.parent / 'aros-core')).resolve()
runner = core / 'developer/debug/test/openfirmware_host/run.py'
if not runner.is_file():
    raise SystemExit('Selected core lacks the shared DeviceTree test: ' + str(runner))
os.execv(sys.executable, [sys.executable, '-B', str(runner), '--source', str(core), *sys.argv[1:]])
