#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Actual dispatch mask function, including rejection of the pre-fix policy."""
from pathlib import Path
import argparse
import subprocess
import tempfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core',type=Path,default=Path(__file__).resolve().parents[2]/'aros-core-smp')
core=parser.parse_args().core.resolve()
s=(core/'arch/aarch64-apple/kernel/kernel_cpu.c').read_text()
start=s.index('void cpu_ApplyTaskMasks(')
func=s[start:s.index('\n}',start)+2]
prelude='''#include <stdint.h>
#include <aros/apple/runtime_local.h>
#define TASK_SPSR_IRQ_MASK UINT64_C(0x80)
#define TASK_SPSR_FIQ_MASK UINT64_C(0x40)
static struct AROSAppleRuntimeLocal cpu;
struct AROSAppleRuntimeLocal *AROSAppleRuntimeLocal(void) { return &cpu; }
'''
check='''
int main(void) {
 for (unsigned r=0;r<4;r++) for(int n=-1;n<2;n++)
 for(unsigned d=0;d<3;d++) for(unsigned h=0;h<3;h++) {
  cpu.irq_ready=r&1;cpu.fiq_ready=r&2;
  cpu.interrupt_depth=d;cpu.irq_mask_depth=h;
  uint64_t spsr=0xa0000340;
  uint64_t expected=n>=0||h ? 0xc0 : ((r&1?0:0x80)|(r&2?0:0x40));
  cpu_ApplyTaskMasks(&spsr,n);
  if((spsr&0xc0)!=expected || (spsr&~UINT64_C(0xc0))!=(UINT64_C(0xa0000340)&~UINT64_C(0xc0))) return 1;
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 for name,body,expected in [('production',func,0),('pre-fix',func.replace('AROSAppleRequiredTaskMasks','AROSAppleRequiredIRQMasks'),1)]:
  src=Path(d)/(name+'.c');exe=Path(d)/name
  src.write_text(prelude+body+check)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(core/'arch/aarch64-apple/include'),str(src),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)])
  assert result.returncode==expected,(name,result.returncode)
print('PASS dispatch masks: production function; pre-fix interrupt-depth policy rejected')
