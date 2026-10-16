/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Polling early console using Virtio 1.2 split queue 1 (port-zero transmit).
 * The loader retains this identity-mapped record for bootstrap and kernel.
 */
#ifndef POMAROS_VM_CONSOLE_H
#define POMAROS_VM_CONSOLE_H
#include <stdint.h>
#include <stddef.h>
#define VM_CONSOLE_MAGIC UINT64_C(0x564d434f4e534f4c)
struct VMConsoleDesc { uint64_t address; uint32_t length; uint16_t flags, next; };
struct VMConsoleRing {
    struct VMConsoleDesc desc[8];
    uint16_t flags,index,ring[8],event,padding;
    volatile uint16_t used_flags,used_index;
    volatile struct { uint32_t id,length; } used[8];
    uint64_t doorbell;
};
struct VMConsoleControl { uint32_t id;uint16_t event,value; };
struct VMConsole {
    uint64_t magic, common, notify, region_start, region_end;
    uint32_t multiplier, notify_length;
    uint16_t index, initialized, failed, reserved;
    uint64_t doorbell;
    struct VMConsoleDesc desc[8];
    uint16_t avail_flags, avail_index, avail_ring[8], unused_event;
    uint32_t used_padding;
    volatile uint16_t used_flags, used_index;
    volatile struct { uint32_t id, length; } used_ring[8];
    uint8_t bytes[256];
    _Alignas(16) uint8_t receive_desc[128];
    _Alignas(4) uint8_t receive_avail[24];
    _Alignas(4) uint8_t receive_used[72];
    _Alignas(16) struct VMConsoleRing control_rx,control_tx;
    struct VMConsoleControl control_message;
    _Alignas(16) uint8_t control_buffers[8][64];
};
_Static_assert(sizeof(struct VMConsole) <= 4096, "early console allocation");
_Static_assert(offsetof(struct VMConsole,desc)%16 == 0,"descriptor alignment");
_Static_assert(offsetof(struct VMConsole,used_flags)%4 == 0,"used alignment");
#define VMREG(type, base, offset) (*(volatile type *)(uintptr_t)((base)+(offset)))
static inline int vm_control_queue(struct VMConsole *s,unsigned id,struct VMConsoleRing *ring)
{
    uint64_t c=s->common;
    VMREG(uint16_t,c,22)=(uint16_t)id;
    if (VMREG(uint16_t,c,24)<8) return 0;
    VMREG(uint16_t,c,24)=8;VMREG(uint16_t,c,26)=0xffff;
    uint64_t offset=(uint64_t)VMREG(uint16_t,c,30)*s->multiplier;
    if (s->notify_length<2 || offset>s->notify_length-2) return 0;
    ring->doorbell=s->notify+offset;
    VMREG(uint64_t,c,32)=(uintptr_t)ring->desc;
    VMREG(uint64_t,c,40)=(uintptr_t)&ring->flags;
    VMREG(uint64_t,c,48)=(uintptr_t)&ring->used_flags;
    ring->flags=1;
    __asm__ volatile("dsb sy" ::: "memory");
    VMREG(uint16_t,c,28)=1;
    return 1;
}
static inline int vm_control_send(struct VMConsole *s,uint16_t event,uint16_t value)
{
    struct VMConsoleRing *r=&s->control_tx;
    s->control_message.id=0;s->control_message.event=event;s->control_message.value=value;
    r->desc[0].address=(uintptr_t)&s->control_message;r->desc[0].length=8;
    r->ring[r->index%8]=0;
    __asm__ volatile("dsb sy" ::: "memory");
    ++r->index;
    __asm__ volatile("dsb sy" ::: "memory");
    VMREG(uint16_t,r->doorbell,0)=3;
    for(unsigned spin=0;r->used_index!=r->index;++spin) {
        if(spin==10000000) return 0;
        __asm__ volatile("dmb sy" ::: "memory");
    }
    return r->used[(r->index-1)%8].id==0;
}
static inline int vm_control_open(struct VMConsole *s)
{
    struct VMConsoleRing *r=&s->control_rx;
    for(unsigned i=0;i<8;++i) {
        r->desc[i].address=(uintptr_t)s->control_buffers[i];r->desc[i].length=64;
        r->desc[i].flags=2;r->ring[i]=(uint16_t)i;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    r->index=8;
    __asm__ volatile("dsb sy" ::: "memory");
    VMREG(uint16_t,r->doorbell,0)=2;
    if(!vm_control_send(s,0,1)) return 0; /* DEVICE_READY */
    uint16_t used=0;
    for(unsigned spin=0;spin<50000000;++spin) {
        __asm__ volatile("dmb sy" ::: "memory");
        if(r->used_index==used) continue;
        uint32_t id=r->used[used%8].id,length=r->used[used%8].length;
        if(id>=8 || length<8 || length>64) return 0;
        struct VMConsoleControl *event=(struct VMConsoleControl *)s->control_buffers[id];
        int opened=event->id==0 && event->event==6 && event->value==1;
        if(event->id==0 && event->event==1) { /* DEVICE_ADD */
            if(!vm_control_send(s,3,1) || !vm_control_send(s,6,1)) return 0;
        }
        ++used;r->ring[r->index%8]=(uint16_t)id;
        __asm__ volatile("dsb sy" ::: "memory");
        ++r->index;
        __asm__ volatile("dsb sy" ::: "memory");
        VMREG(uint16_t,r->doorbell,0)=2;
        if(opened) return 1;
    }
    return 0;
}
static inline int vm_console_init(struct VMConsole *s)
{
    uint64_t c=s->common;
    if (s->magic!=VM_CONSOLE_MAGIC || s->failed || s->region_end<s->region_start ||
        c<s->region_start || c>s->region_end || s->region_end-c<56 ||
        s->notify<s->region_start || s->notify>s->region_end ||
        s->notify_length>s->region_end-s->notify || (c&7) || (s->notify&1)) return 0;
    if (s->initialized) return 1;
    s->reserved=1;
    VMREG(uint8_t,c,20)=0;
    for (unsigned i=0;VMREG(uint8_t,c,20)!=0;++i) if(i==1000000) goto fail;
    VMREG(uint8_t,c,20)=3;
    s->reserved=2;
    VMREG(uint32_t,c,0)=1;
    if (!(VMREG(uint32_t,c,4)&1)) goto fail;
    VMREG(uint32_t,c,0)=0;
    uint32_t multiport=VMREG(uint32_t,c,4)&2;
    VMREG(uint32_t,c,8)=0;VMREG(uint32_t,c,12)=multiport;
    VMREG(uint32_t,c,8)=1;VMREG(uint32_t,c,12)=1;
    s->reserved=3;
    VMREG(uint8_t,c,20)=11;
    if (!(VMREG(uint8_t,c,20)&8)) goto fail;
    s->reserved=4;
    VMREG(uint16_t,c,22)=0;
    if (VMREG(uint16_t,c,24)<8) goto fail;
    VMREG(uint16_t,c,24)=8;VMREG(uint16_t,c,26)=0xffff;
    VMREG(uint64_t,c,32)=(uintptr_t)s->receive_desc;
    VMREG(uint64_t,c,40)=(uintptr_t)s->receive_avail;
    VMREG(uint64_t,c,48)=(uintptr_t)s->receive_used;
    VMREG(uint16_t,c,28)=1;
    VMREG(uint16_t,c,22)=1;
    if (VMREG(uint16_t,c,24)<8) goto fail;
    VMREG(uint16_t,c,24)=8;VMREG(uint16_t,c,26)=0xffff;
    s->reserved=5;
    uint64_t offset=(uint64_t)VMREG(uint16_t,c,30)*s->multiplier;
    if (s->notify_length<2 || offset>s->notify_length-2) goto fail;
    s->doorbell=s->notify+offset;
    s->avail_flags=1;
    VMREG(uint64_t,c,32)=(uintptr_t)s->desc;
    VMREG(uint64_t,c,40)=(uintptr_t)&s->avail_flags;
    VMREG(uint64_t,c,48)=(uintptr_t)&s->used_flags;
    __asm__ volatile("dsb sy" ::: "memory");
    VMREG(uint16_t,c,28)=1;
    if (multiport && (!vm_control_queue(s,2,&s->control_rx) ||
                      !vm_control_queue(s,3,&s->control_tx))) goto fail;
    VMREG(uint8_t,c,20)=15;
    s->reserved=8;
    if(multiport && !vm_control_open(s)) goto fail;
    VMREG(uint16_t,c,22)=1;
    s->reserved=6;
    s->initialized=1;
    return 1;
fail:
    s->failed=1; return 0;
}
#ifndef VM_CONSOLE_TICKS
static inline uint64_t vm_console_ticks(void)
{
    uint64_t value;__asm__ volatile("mrs %0,cntvct_el0":"=r"(value));return value;
}
#define VM_CONSOLE_TICKS() vm_console_ticks()
#endif
#ifndef VM_CONSOLE_FREQUENCY
static inline uint64_t vm_console_frequency(void)
{
    uint64_t value;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(value));return value;
}
#define VM_CONSOLE_FREQUENCY() vm_console_frequency()
#endif
/* A timeout does not revoke device ownership or prove a broken queue. Keep
 * the descriptor/buffer intact and drain it before a later write reuses it. */
static inline int vm_console_drain(struct VMConsole *s)
{
    uint64_t start=VM_CONSOLE_TICKS(),limit=VM_CONSOLE_FREQUENCY();
    if(!limit)return 0;
    while(s->used_index!=s->index) {
        if(VM_CONSOLE_TICKS()-start>=limit)return 0;
        __asm__ volatile("dmb sy" ::: "memory");
    }
    __asm__ volatile("dmb sy" ::: "memory");
    if(s->used_ring[(uint16_t)(s->index-1)%8].id!=0) { s->failed=1;return 0; }
    return 1;
}
static inline int vm_console_write(struct VMConsole *s,const char *text,size_t length)
{
    if (!vm_console_init(s) || !vm_console_drain(s)) return 0;
    while(length) {
        size_t n=length>sizeof(s->bytes)?sizeof(s->bytes):length;
        for(size_t i=0;i<n;++i) s->bytes[i]=(uint8_t)text[i];
        s->desc[0].address=(uintptr_t)s->bytes;s->desc[0].length=(uint32_t)n;
        s->desc[0].flags=0;s->desc[0].next=0;
        s->avail_ring[s->index%8]=0;
        __asm__ volatile("dsb sy" ::: "memory");
        s->avail_index=++s->index;
        __asm__ volatile("dsb sy" ::: "memory");
        VMREG(uint16_t,s->doorbell,0)=1;
        s->reserved=7;
        if(!vm_console_drain(s))return 0;
        text+=n;length-=n;
    }
    return 1;
}
#undef VMREG
#endif
