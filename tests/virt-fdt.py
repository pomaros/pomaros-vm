#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
core=Path(os.environ.get('AROS_CORE_SOURCE',root.parent/'aros-core-vm'))
arch=core/'arch/aarch64-apple'
with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp);exe=work/'test'
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(arch/'include'),'-I'+str(arch/'bootstrap'),str(root/'tests/virt-fdt.c'),
        str(arch/'kernel/memory_plan_contract.c'),str(arch/'bootstrap/boot_memory_map.c'),
        str(arch/'bootstrap/stage2_fdt_map.c'),'-o',str(exe)],check=True)
    cases=[('0x09000000','0x1000','',1),('0x0a000000','0x2000','',1),
           ('0x40000000','0x1000','',0),('0x09000001','0x1000','',0),
           ('0x09000000','0x20','',0),('0x09000000','0x1000','status="disabled";',0)]
    for address,size,extra,expected in cases:
        dts=work/'input.dts';dtb=work/'input.dtb'
        dts.write_text('/dts-v1/; / { compatible="linux,dummy-virt"; #address-cells=<2>; #size-cells=<2>; '
            f'uart@{int(address,0):x} {{ compatible="arm,pl011"; reg=<0 {address} 0 {size}>; {extra} }}; }};')
        subprocess.run(['dtc','-q','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
        subprocess.run([str(exe),str(dtb),str(expected),address],check=True,timeout=5)
print('virt-fdt result=PASS cases=6 sanitizers=ASan,UBSan')
