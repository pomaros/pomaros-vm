/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper */
#include <assert.h>
#include <stdio.h>
#include <aros/apple/runtime_local.h>
int main(void)
{
    struct AROSAppleRuntimeLocal cpu={0};
    for (unsigned ready=0;ready<4;ready++) {
        cpu.irq_ready=ready&1;cpu.fiq_ready=ready&2;
        unsigned expected=(cpu.irq_ready?0:0x80)|(cpu.fiq_ready?0:0x40);
        assert(AROSAppleRequiredIRQMasks(&cpu,-1)==expected);
        assert(AROSAppleRequiredIRQMasks(&cpu,0)==0xc0);
        assert(AROSAppleRequiredIRQMasks(&cpu,7)==0xc0);
        cpu.irq_mask_depth=1;
        assert(AROSAppleRequiredIRQMasks(&cpu,-1)==0xc0);
        cpu.irq_mask_depth=2;
        assert(AROSAppleRequiredIRQMasks(&cpu,-1)==0xc0);
        cpu.irq_mask_depth=0;cpu.interrupt_depth=1;
        assert(AROSAppleRequiredIRQMasks(&cpu,-1)==0xc0);
        /* A dispatched task must not inherit the IRQ handler depth. */
        assert(AROSAppleRequiredTaskMasks(&cpu,-1)==expected);
        cpu.interrupt_depth=0;
        assert(AROSAppleRequiredIRQMasks(&cpu,-1)==expected);
    }
    puts("PASS IRQ policy: readiness, Disable nesting, interrupt and local hold depth");
}
