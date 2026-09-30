/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 */
.section .text.entry,"ax"
.global _start
_start:
    msr daifset, #15
    mrs x1, mpidr_el1
    and x1, x1, #0xff
    cbnz x1, .Lpark
    adr x1, stack_top
    mov sp, x1
    mrs x2, CurrentEL
    cmp x2, #8
    b.ne .Lel1
    msr sp_el1, x1
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    mov x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    mov x1, #0x3c5
    msr spsr_el2, x1
    adr x1, .Lel1
    msr elr_el2, x1
    eret
.Lel1:
    adr x1, stack_top
    mov sp, x1
    adr x1, bss_start
    adr x2, bss_end
.Lzero:
    cmp x1, x2
    b.hs .Lrun
    str xzr, [x1], #8
    b .Lzero
.Lrun:
    bl probe
.Lpark:
    wfi
    b .Lpark
.section .bss,"aw",%nobits
.balign 16
.space 65536
stack_top:

/* EL1 vector slots. Every IRQ entry preserves all general registers, x18
 * included. FP/SIMD remains disabled in this freestanding qualification guest. */
.section .text.vectors,"ax"
.balign 2048
.global probe_vectors
probe_vectors:
.macro SLOT destination
    b \destination
    .balign 128
.endm
SLOT .Lexception
SLOT .Lirq
SLOT .Lexception
SLOT .Lexception
SLOT .Lexception
SLOT .Lirq
SLOT .Lexception
SLOT .Lexception
SLOT .Lexception
SLOT .Lirq
SLOT .Lexception
SLOT .Lexception
SLOT .Lexception
SLOT .Lirq
SLOT .Lexception
SLOT .Lexception
.Lirq:
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    bl probe_irq
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    .global probe_restore_x18
probe_restore_x18:
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256
    eret
.Lexception:
    msr daifset, #15
    bl probe_exception
    b .Lpark

.section .text.wait,"ax"
.global probe_wait
probe_wait:
    mov x18, #0x1234
.Lticks:
    msr daifset, #2
    ldr w9, [x0]
    cmp w9, #128
    b.hs .Lchecked
    wfi
    msr daifclr, #2
    isb
    b .Lticks
.Lchecked:
    mov x10, #0x1234
    cmp x18, x10
    cset w0, eq
    ret
