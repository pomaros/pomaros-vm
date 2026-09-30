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
fdt=core/'arch/aarch64-all/boot/fdt'
with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp);exe=work/'test'
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(fdt),'-I'+str(fdt/'libfdt'),str(fdt/'early_fdt.c'),
        *[str(fdt/'libfdt'/name) for name in ['fdt.c','fdt_ro.c','fdt_check.c']],
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
        subprocess.run([str(exe),str(dtb),str(expected),address,"1","1"],check=True,timeout=5)
    # GIC sorts before the console; console identity must survive sorting.
    def controller(base='0x08000000', second='0x080a0000', size='0xf60000',
                   compatible='arm,gic-v3', extra='', name='intc'):
        return (f'{name}@{int(base,0):x} {{ compatible="{compatible}"; '
                f'reg=<0 {base} 0 0x10000 0 {second} 0 {size}>; '
                f'interrupt-controller; #interrupt-cells=<3>; {extra} }};')
    cases=[(controller(),1,2,3),
           (controller(second='0x08010000',size='0x2000',compatible='arm,gic-400'),1,2,3),
           (controller(second='0x40000000'),0,0,0),
           (controller(second='0x09000000'),0,0,0),
           (controller(second='0x08008000'),0,0,0),
           (controller(second='0x080a0001'),0,0,0),
           (controller(size='0'),0,0,0),
           (controller(extra='status="disabled";'),1,1,1),
           (controller()+controller(base='0x10000000',name='other'),0,0,0)]
    for controller_dts,expected,ranges,roles in cases:
        dts.write_text('/dts-v1/; / { compatible="linux,dummy-virt"; #address-cells=<2>; #size-cells=<2>; '
            'chosen { stdout-path="/uart@9000000"; }; '
            'uart@9000000 { compatible="arm,pl011"; reg=<0 0x09000000 0 0x1000>; }; '
            +controller_dts+'};')
        subprocess.run(['dtc','-q','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
        subprocess.run([str(exe),str(dtb),str(expected),'0x09000000',str(ranges),str(roles)],
                       check=True,timeout=5)
print('virt-fdt result=PASS cases=15 sanitizers=ASan,UBSan')
