#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
import subprocess
import sys
try:
    result = subprocess.run(sys.argv[2:], timeout=float(sys.argv[1]))
    sys.exit(result.returncode)
except subprocess.TimeoutExpired:
    sys.exit('FAIL: guest exceeded deadline')
