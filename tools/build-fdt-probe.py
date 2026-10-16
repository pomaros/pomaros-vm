#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
core=Path(os.environ.get('AROS_CORE_SOURCE',root.parent/'aros-core-pomaros'))
irq=core/'arch/aarch64-all/irq'
fdt=core/'arch/aarch64-all/boot/fdt';out=root/'build/fdt-probe';out.mkdir(parents=True,exist_ok=True)
sources=[root/'guest/fdt/entry.S',root/'guest/fdt/probe.c',fdt/'early_fdt.c',irq/'gic.c',
         *[fdt/'libfdt'/n for n in ['fdt.c','fdt_ro.c','fdt_check.c']]]
objects=[]
for source in sources:
 obj=out/(source.stem+'.o');objects.append(obj)
 subprocess.run(['clang','--target=aarch64-none-elf','-O2','-Wall','-Wextra','-Werror',
  '-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-mstrict-align',
  '-ffunction-sections','-fdata-sections','-DAROS_FDT_FREESTANDING',
  '-I'+str(fdt),'-I'+str(fdt/'libfdt'),'-I'+str(irq),'-c',str(source),'-o',str(obj)],check=True)
for platform,base in [('virt','0x40080000'),('rpi4','0x80000')]:
 subprocess.run(['ld.lld','-T',str(root/'guest/fdt/link.ld'),'--defsym=IMAGE_BASE='+base,
  '--gc-sections','-o',str(out/(platform+'.elf')),*map(str,objects)],check=True)
 subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objcopy','-O','binary',str(out/(platform+'.elf')),str(out/(platform+'.bin'))],check=True)
print('build-fdt-probe result=PASS guest-x18=unrestricted')
