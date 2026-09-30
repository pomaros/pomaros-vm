// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
.global _start
_start:
    mov x18, #0x1234
    hvc #0x18
    add x18, x18, #1
    hvc #0x19
    b .
