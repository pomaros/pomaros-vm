#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Check the generated AArch64 MMIO transactions, not a RAM-only simulation."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
core = Path(os.environ.get('AROS_CORE_SOURCE', root.parent / 'aros-core-pomaros'))
with tempfile.TemporaryDirectory(prefix='ecam-codegen-') as tmp:
    source = Path(tmp) / 'stores.c'
    assembly = Path(tmp) / 'stores.s'
    source.write_text('#include "ecam/access.h"\n' + '\n'.join(
        f'int write{width}(const struct AROSPCIECAM *h,unsigned b,unsigned d,unsigned f,unsigned r,uint32_t v)'
        f'{{return AROSPCIECAMWrite(h,b,d,f,r,{width},v);}}'
        for width in (1, 2, 4)))
    subprocess.run(['clang', '--target=aarch64-none-elf', '-O2', '-S',
                    '-I'+str(core/'rom/hidds/pci'), str(source), '-o', str(assembly)], check=True)
    text = assembly.read_text()
    for width, opcode in ((1, 'strb'), (2, 'strh'), (4, 'str')):
        body = text.split(f'write{width}:', 1)[1].split('.Lfunc_end', 1)[0]
        stores = re.findall(r'^\s*(st\w+)\s+([^\n]+)', body, re.M)
        assert len(stores) == 1 and stores[0][0] == opcode, (width, stores)
        assert stores[0][1].startswith('w'), (width, stores)
print('pci-ecam-codegen result=PASS AArch64=strb,strh,str-w single-store no-RMW')
