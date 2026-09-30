// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static uint64_t ticks(void);
#define VM_CONSOLE_TICKS() ticks()
#define VM_CONSOLE_FREQUENCY() 3
#include "../include/vm-console.h"
static struct VMConsole state;
static uint64_t now;
static int complete;
static uint64_t ticks(void)
{
    if(complete && state.used_index!=state.index) {
        state.used_ring[(uint16_t)(state.index-1)%8].id=0;
        state.used_index=state.index;
    }
    return now++;
}
int main(void)
{
    _Alignas(8) uint8_t mmio[128]={0};
    state.magic=VM_CONSOLE_MAGIC;state.common=state.region_start=(uintptr_t)mmio;
    state.region_end=state.region_start+sizeof(mmio);
    state.notify=state.doorbell=state.region_start+64;state.notify_length=2;
    state.initialized=1;
    assert(!vm_console_write(&state,"A",1));
    assert(!state.failed && state.index==1 && state.avail_index==1 && state.bytes[0]=='A');
    assert(!vm_console_write(&state,"B",1));
    assert(!state.failed && state.index==1 && state.bytes[0]=='A');
    complete=1;
    assert(vm_console_write(&state,"B",1));
    assert(!state.failed && state.index==2 && state.used_index==2 && state.bytes[0]=='B');
    state.index=state.avail_index=state.used_index=65535;
    assert(vm_console_write(&state,"C",1));
    assert(!state.index && !state.used_index && state.bytes[0]=='C');
    state.used_ring[7].id=7;
    assert(!vm_console_write(&state,"D",1) && state.failed && state.bytes[0]=='C');
    puts("console-timeout result=PASS bounded-deadline,pending-ownership,late-completion,recovery,wrap,bad-id");
}
