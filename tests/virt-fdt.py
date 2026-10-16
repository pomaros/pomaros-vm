#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
core=Path(os.environ.get('AROS_CORE_SOURCE',root.parent/'aros-core-pomaros'))
arch=core/'arch/aarch64-apple'
fdt=core/'arch/aarch64-all/boot/fdt'
with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp);exe=work/'test'
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-I'+str(fdt),'-I'+str(fdt/'libfdt'),str(fdt/'early_fdt.c'),str(fdt/'pci_ecam.c'),
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
    def pci(ecam='0x30000000', low='0x10000000', flags='0x02000000',
            first=0, last=255, extra='', high='0x80', cells=3):
        return (f'pcie@30000000 {{ compatible="pci-host-ecam-generic"; device_type="pci"; '
                f'#address-cells=<{cells}>; #size-cells=<2>; reg=<0 {ecam} 0 0x10000000>; '
                f'bus-range=<{first} {last}>; '
                f'ranges=<{flags} 0 {low} 0 {low} 0 0x100000 '
                f'0x03000000 {high} 0 {high} 0 0 0x100000>; {extra} }};')
    cases=[(pci(),1,5,6), (pci(first=128),1,5,6),
           (pci(low='0x40000000'),0,0,0), (pci(ecam='0x40000000'),0,0,0),
           (pci(low='0x08000000'),0,0,0), (pci(low='0x30000000'),0,0,0),
           (pci(low='0x10000001'),0,0,0), (pci(ecam='0x30001000'),0,0,0),
           (pci(flags='0x12000000'),0,0,0), (pci(first=2,last=1),0,0,0),
           (pci(last=256),0,0,0), (pci(high='0x10000'),0,0,0),
           (pci(cells=2),0,0,0), (pci(extra='status="disabled";'),1,2,3),
           (pci()+pci().replace('pcie@30000000','other@30000000'),0,0,0)]
    for pci_dts,expected,ranges,roles in cases:
        dts.write_text('/dts-v1/; / { compatible="linux,dummy-virt"; #address-cells=<2>; #size-cells=<2>; '
            'chosen { stdout-path="/uart@9000000"; }; '
            'uart@9000000 { compatible="arm,pl011"; reg=<0 0x09000000 0 0x1000>; }; '
            +controller()+pci_dts+'};')
        subprocess.run(['dtc','-q','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
        subprocess.run([str(exe),str(dtb),str(expected),'0x09000000',str(ranges),str(roles)],
                       check=True,timeout=5)
    def frame(base='0x20000000', size='0x1000', extra='', marker='msi-controller;', reg_extra=''):
        return (f'v2m@20000000 {{ compatible="arm,gic-v2m-frame"; phandle=<2>; '
                f'reg=<0 {base} 0 {size} {reg_extra}>; {marker} {extra} }};')
    def msi_gic(child, ranges='ranges;', extra=''):
        return controller(extra='#address-cells=<2>; #size-cells=<2>; '+ranges+extra+child)
    cases=[(msi_gic(frame()),'msi-parent=<2>;',1,6,7),
           (msi_gic(frame(extra='arm,msi-base-spi=<128>; arm,msi-num-spis=<128>;')),'msi-parent=<2>;',1,6,7),
           (msi_gic(frame(extra='#msi-cells=<0>;')),'msi-parent=<2>;',1,6,7),
           (msi_gic(frame()),'',1,5,6),  # Unassociated registers stay unmapped.
           (msi_gic(frame()),'msi-parent=<3>;',0,0,0),
           (msi_gic(frame()),'msi-parent=<0>;',0,0,0),
           (msi_gic(frame()),'msi-parent=<2 0>;',0,0,0),
           (msi_gic(frame(extra='status="disabled";')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(extra='#msi-cells=<1>;')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(marker='')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(extra='arm,msi-base-spi=<128>;')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(extra='arm,msi-base-spi=<31>; arm,msi-num-spis=<1>;')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(extra='arm,msi-base-spi=<1000>; arm,msi-num-spis=<21>;')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(base='0x40000000')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(base='0x08000000')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(base='0x10000000')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(base='0x30000000')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(base='0x20000001')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(size='0x2000')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(reg_extra='0 0x20001000 0 0x1000')),'msi-parent=<2>;',0,0,0),
           (msi_gic(frame(),ranges=''),'msi-parent=<2>;',0,0,0)]
    for gic_dts,msi_parent,expected,ranges,roles in cases:
        dts.write_text('/dts-v1/; / { compatible="linux,dummy-virt"; #address-cells=<2>; #size-cells=<2>; '
            'chosen { stdout-path="/uart@9000000"; }; '
            'uart@9000000 { compatible="arm,pl011"; reg=<0 0x09000000 0 0x1000>; }; '
            +gic_dts+pci(extra=msi_parent)+'};')
        subprocess.run(['dtc','-q','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
        subprocess.run([str(exe),str(dtb),str(expected),'0x09000000',str(ranges),str(roles)],
                       check=True,timeout=5)
print('virt-fdt result=PASS cases=51 sanitizers=ASan,UBSan-fatal')
