#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
subprocess.run([str(root/'build/x18-probe'),str(root/'build/x18.bin')],check=True,timeout=10)
with tempfile.TemporaryDirectory() as work:
    broken=Path(work)/'broken.bin'
    code=bytearray((root/'build/x18.bin').read_bytes())
    code[:4]=bytes.fromhex('1f2003d5')  # NOP: x18 never receives its test value.
    broken.write_bytes(code)
    result=subprocess.run([str(root/'build/x18-probe'),str(broken)],capture_output=True,timeout=10)
    assert result.returncode != 0 and b'FAIL reason=' in result.stderr
print('x18 negative-control=PASS')
