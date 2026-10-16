#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Compile upstream Exec accessors for privileged native and EL0 Apple backends."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--core',type=Path,default=root.parent/'aros-core-smp')
p.add_argument('--build',type=Path,default=Path.home()/'Source/aros-project/Build/macaros-vm/smp-core')
a=p.parse_args();core=a.core.resolve();build=a.build.resolve()
compiler=Path.home()/'Source/aros-project/aros-crosstools/bin/clang'
with tempfile.TemporaryDirectory() as temp:
    tmp=Path(temp)
    policy=tmp/'irq-policy'
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-I'+str(core/'arch/aarch64-apple/include'),
        str(root/'tests/irq-mask-policy.c'),'-o',str(policy)],check=True,timeout=30)
    subprocess.run([str(policy)],check=True,timeout=5)
    common=[str(compiler),'--target=aarch64-unknown-aros','-O2','-ffreestanding',
        '-Wall','-Wextra','-Werror','-D__AROSEXEC_SMP__=','-S',str(root/'tests/exec-tls.c')]
    sdk=['-I'+str(build/'bin/apple-aarch64/gen/include'),
         '-I'+str(build/'bin/apple-aarch64/AROS/Developer/include'),
         '-I'+str(core/'compiler/arossupport/include')]
    results={}
    for backend in ('apple','native'):
        inc=core/('arch/aarch64-apple/exec' if backend=='apple' else 'arch/aarch64-native/exec')
        extra=['-I'+str(inc)]
        if backend=='apple':
            extra+=['-DTEST_APPLE_EL0','-I'+str(core/'arch/aarch64-apple/include')]
        else:
            extra+=['-I'+str(core/'arch/aarch64-native/kernel')]
        output=tmp/(backend+'.s')
        subprocess.run(common+extra+sdk+['-o',str(output)],check=True,timeout=30)
        results[backend]=output.read_text().lower()
    apple=results['apple'];native=results['native']
    assert apple.count('tpidrro_el0')==10, 'Every Apple TLS accessor and IRQ bridge uses the CPU anchor'
    assert not re.search(r'\b(tpidr_el1|daif|daifset|daifclr)\b',apple), 'Privileged access in EL0 adapter'
    assert len(re.findall(r'\b(?:br|blr)\b',apple))==2, 'IRQ exclusion calls the kernel callback table'
    assert native.count('tpidr_el1')==8 and 'daifset' in native
    print('PASS Exec TLS: eight EL0 accessors, runtime IRQ bridge, native privileged backend retained')
