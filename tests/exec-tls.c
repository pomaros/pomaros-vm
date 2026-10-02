/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper */
#include <exec/types.h>
#include <exec/tasks.h>
#include <atomic.h>
#if defined(TEST_APPLE_EL0)
#include "exec_platform_smp.h"
#else
#include "exec_platform.h"
#endif
struct Task *current_task(void) { return GET_THIS_TASK; }
unsigned current_cpu(void) { return TLS_GET(CPUNumber); }
int interrupt_nesting(void) { return IDNESTCOUNT_GET; }
void set_interrupt_nesting(int n) { IDNESTCOUNT_SET(n); }
void set_quantum(unsigned q) { SCHEDQUANTUM_SET(q); }
void increment_dispatch_depth(void) { EXEC_BLOCK_DISPATCH_INC; }
void decrement_dispatch_depth(void) { EXEC_BLOCK_DISPATCH_DEC; }
void flag_switch(void) { FLAG_SCHEDSWITCH_SET; }
unsigned long mask_local(void) { return EXEC_IRQFIQ_DISABLE(); }
void restore_local(unsigned long mask) { EXEC_IRQFIQ_RESTORE(mask); }
