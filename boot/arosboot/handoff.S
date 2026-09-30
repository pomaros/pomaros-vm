// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

/*
 * Transfer from the AArch64 UEFI environment to the AROS entry contract.
 *
 * x0 = physical AROSBootInfo address
 * x1 = physical top of loader-owned stack
 * x2 = physical payload entry address
 */

    .text
    .p2align 4
    .globl arosboot_enter_payload
arosboot_enter_payload:
    msr     daifset, #0xf
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2

    /* The producer has cleaned every handoff range to the point of coherency. */
    dsb     sy
    ic      iallu
    dsb     sy
    isb

    mrs     x9, CurrentEL
#if defined(AROSBOOT_QEMU_TEST)
    mov     x22, x9
    cmp     x22, #8
    b.eq    .Lreport_source_el2
    mov     x0, #4
    adr     x1, .Lsource_el1_message
    hlt     #0xf000
    b       .Lsource_reported
.Lreport_source_el2:
    mov     x0, #4
    adr     x1, .Lsource_el2_message
    hlt     #0xf000
.Lsource_reported:
    mov     x0, x19
    mov     x9, x22
#endif
    cmp     x9, #8
    b.eq    .Lfrom_el2
    cmp     x9, #4
    b.eq    .Lfrom_el1
    b       .Lunsupported_el

.Lfrom_el2:
    /* Preserve timer access and select the non-VHE AArch64 EL1 regime. */
    mrs     x9, cnthctl_el2
    orr     x9, x9, #3
    msr     cnthctl_el2, x9
    mov     x9, #0x80000000
    msr     hcr_el2, x9
    msr     vttbr_el2, xzr
    isb

    /*
     * CPTR_EL2 changes layout with the effective HCR_EL2.E2H value.  Do not
     * clear it while firmware is still in VHE: zero means FPEN=0 there and
     * traps the first Advanced SIMD instruction used by the EL1 payload.
     * Select the trap-free FP value only after HCR_EL2 has synchronized, and
     * honor implementations which retain E2H despite the requested value.
     */
    mrs     x10, hcr_el2
    tbz     x10, #34, .Lcptr_non_vhe
    mov     x10, #0x300000
    msr     cptr_el2, x10
    b       .Lcptr_ready
.Lcptr_non_vhe:
    msr     cptr_el2, xzr
.Lcptr_ready:
    isb

    /* EL1 starts little-endian with SCTLR_EL1.A/M/C/I disabled. */
    movz    x9, #0x0808
    movk    x9, #0x30d0, lsl #16
    msr     sctlr_el1, x9
    msr     sp_el1, x20

    /* Stop using the firmware's EL2 translation and cache regime. */
    mrs     x9, sctlr_el2
    bic     x9, x9, #1
    bic     x9, x9, #4
    bic     x9, x9, #0x1000
    bic     x9, x9, #0x2000000
    msr     sctlr_el2, x9
    isb

    adr     x9, .Lel1_entry
    msr     elr_el2, x9
    mov     x9, #0x3c5
    msr     spsr_el2, x9
    mov     x0, x19
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    eret

.Lfrom_el1:
    /* Identity mapping is guaranteed for UEFI RAM, so execution continues. */
    mrs     x9, sctlr_el1
    bic     x9, x9, #1
    bic     x9, x9, #2
    bic     x9, x9, #4
    bic     x9, x9, #0x1000
    bic     x9, x9, #0x3000000
    orr     x9, x9, #8
    msr     sctlr_el1, x9
    isb

.Lel1_entry:
    msr     spsel, #1
    mov     sp, x20
    mov     x0, x19
#if AROSBOOT_ENTRY_NEGATIVE_CONTROL
    mov     x1, #1
#else
    mov     x1, xzr
#endif
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    br      x21

.Lunsupported_el:
#if defined(AROSBOOT_QEMU_TEST)
    mov     x0, #4
    adr     x1, .Lunsupported_message
    hlt     #0xf000
#endif
1:
    wfe
    b       1b

#if defined(AROSBOOT_QEMU_TEST)
    .section .rdata,"dr"
.Lunsupported_message:
    .asciz  "[A0Q] FAIL: UEFI entered at an unsupported exception level\n"
.Lsource_el1_message:
    .asciz  "[A0Q] source EL1\n"
.Lsource_el2_message:
    .asciz  "[A0Q] source EL2\n"
#endif
