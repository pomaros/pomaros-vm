#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
core=Path(os.environ.get('AROS_CORE_SOURCE',root.parent/'aros-core-smp'))
src=core/'arch/aarch64-all/boot/fdt'
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp);exe=d/'test';dts=d/'test.dts';dtb=d/'test.dtb'
    dts.write_text('''/dts-v1/; / { #address-cells=<2>; #size-cells=<2>;
      psci { compatible="arm,psci-0.2"; method="hvc"; };
      cpus { #address-cells=<2>; #size-cells=<0>;
        cpu@0 { device_type="cpu"; reg=<0 0>; enable-method="psci"; };
        cpu@100 { device_type="cpu"; reg=<0 0x100>; enable-method="psci"; };
        cpu@200 { device_type="cpu"; reg=<0 0x200>; status="disabled"; };
      }; };''')
    subprocess.run(['dtc','-q','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
    subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-I'+str(src),'-I'+str(src/'libfdt'),str(root/'tests/cpu-monitor.c'),
        *[str(src/n) for n in ('cpu_topology.c','cpu_accounting.c','early_fdt.c')],
        *[str(src/'libfdt'/n) for n in ('fdt.c','fdt_ro.c','fdt_check.c')],'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(dtb)],check=True,timeout=5)
