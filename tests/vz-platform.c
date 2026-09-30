// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../boot/arosboot/vz_platform.c"
EFI_STATUS arosboot_allocate_owned(struct EFI_BOOT_SERVICES *boot,size_t bytes,
    size_t alignment,uint32_t kind,uint32_t flags,uint32_t module,void **out)
{
    (void)boot;(void)bytes;(void)alignment;(void)kind;(void)flags;(void)module;(void)out;
    abort();
}
void arosboot_free_owned(struct EFI_BOOT_SERVICES *boot,void *address) {
    (void)boot;(void)address;abort();
}
static void put(uint8_t *p,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=(uint8_t)(value>>(i*8));
}
static void sum(uint8_t *p,unsigned size,unsigned offset) {
    p[offset]=0;uint8_t v=0;for(unsigned i=0;i<size;++i) v+=p[i];p[offset]=(uint8_t)-v;
}
int main(void) {
    uint8_t rsdp[36]={0},xsdt[44]={0},mcfg[60]={0};
    memcpy(rsdp,"RSD PTR ",8);rsdp[15]=2;put(rsdp+20,36,4);put(rsdp+24,(uintptr_t)xsdt,8);
    memcpy(xsdt,"XSDT",4);put(xsdt+4,sizeof(xsdt),4);put(xsdt+36,(uintptr_t)mcfg,8);
    memcpy(mcfg,"MCFG",4);put(mcfg+4,sizeof(mcfg),4);put(mcfg+44,0x40000000,8);mcfg[55]=255;
    sum(mcfg,sizeof(mcfg),9);sum(xsdt,sizeof(xsdt),9);sum(rsdp,20,8);sum(rsdp,36,32);
    struct EFI_CONFIGURATION_TABLE table={{0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}},rsdp};
    struct EFI_SYSTEM_TABLE system={0};system.number_of_table_entries=1;system.configuration_table=&table;
    uint64_t address=0;
    assert(find_ecam(&system,&address)==0x40000000 && address==(uintptr_t)rsdp);
    rsdp[8]^=1;assert(find_ecam(&system,&address)==0);rsdp[8]^=1;
    mcfg[9]^=1;assert(find_ecam(&system,&address)==0);mcfg[9]^=1;
    mcfg[54]=1;sum(mcfg,sizeof(mcfg),9);assert(find_ecam(&system,&address)==0);
    mcfg[54]=0;sum(mcfg,sizeof(mcfg),9);
    put(xsdt+4,37,4);sum(xsdt,37,9);assert(find_ecam(&system,&address)==0);
    uint8_t *ecam=malloc(1<<20);assert(ecam);memset(ecam,255,1<<20);
    uint8_t *cfg=ecam+(5<<15);memset(cfg,0,256);put(cfg,0x10431af4,4);
    put(cfg+16,0x10004,4);put(cfg+20,1,4);cfg[0x34]=0x40;
    cfg[0x40]=9;cfg[0x41]=0x50;cfg[0x42]=16;cfg[0x43]=1;
    put(cfg+0x4c,56,4);
    cfg[0x50]=9;cfg[0x51]=0;cfg[0x52]=20;cfg[0x53]=2;
    put(cfg+0x58,0x4000,4);put(cfg+0x5c,16,4);put(cfg+0x60,4,4);
    struct VMConsole state={0};assert(find_console((uintptr_t)ecam,&state));
    assert(state.common==UINT64_C(0x100010000) && state.notify==UINT64_C(0x100014000));
    assert(state.region_end==UINT64_C(0x100015000));
    cfg[0x51]=0x50;assert(!find_console((uintptr_t)ecam,&state));cfg[0x51]=0;
    cfg[0x54]=6;assert(!find_console((uintptr_t)ecam,&state));cfg[0x54]=0;
    put(cfg+0x4c,8,4);assert(!find_console((uintptr_t)ecam,&state));
    _Alignas(8) unsigned char tree[4096];
    memset(tree,0,sizeof(tree));
    assert(make_tree(tree,sizeof(tree),&state,0x1000,NULL)==0);
    assert(fdt_check_full(tree,sizeof(tree))==0);
    assert(fdt_path_offset(tree,"/chosen")>0 && fdt_path_offset(tree,"/console")>0);
    unsigned char guard[128];memset(guard,0xa5,sizeof(guard));
    assert(make_tree(guard,64,&state,0x1000,NULL)==-FDT_ERR_NOSPACE);
    for(unsigned i=64;i<sizeof(guard);++i) assert(guard[i]==0xa5);
    uint8_t madt[100]={0},gtdt[104]={0};
    memcpy(madt,"APIC",4);put(madt+4,84,4);
    madt[44]=12;madt[45]=24;put(madt+52,0x10000000,8);madt[64]=3;
    madt[68]=14;madt[69]=16;put(madt+72,0x10010000,8);put(madt+80,0x20000,4);
    memcpy(gtdt,"GTDT",4);put(gtdt+4,sizeof(gtdt),4);
    put(gtdt+56,30,4);put(gtdt+64,27,4);
    sum(madt,84,9);sum(gtdt,sizeof(gtdt),9);
    struct VZInterrupts irq;
    assert(parse_interrupts(madt,gtdt,&irq));
    assert(irq.distributor==0x10000000 && irq.redistributors==0x10010000);
    assert(irq.timer[0]==30 && irq.timer[1]==27 && irq.flags[1]==4);
    assert(make_tree(tree,sizeof(tree),&state,0x1000,&irq)==0);
    int timer=fdt_path_offset(tree,"/timer");
    assert(fdt_stringlist_search(tree,timer,"interrupt-names","virt")==1);
    const fdt32_t *cells=fdt_getprop(tree,timer,"interrupts",NULL);
    assert(cells && fdt32_to_cpu(cells[4])==11);
    madt[45]=0;sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[45]=99;sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[45]=24;madt[64]=2;sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[64]=3;put(madt+72,0x10000000,8);sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+72,0x10010001,8);sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+72,0x10010000,8);sum(madt,84,9);
    put(gtdt+64,15,4);sum(gtdt,sizeof(gtdt),9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(gtdt+64,27,4);put(gtdt+68,2,4);sum(gtdt,sizeof(gtdt),9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(gtdt+68,8,4);sum(gtdt,sizeof(gtdt),9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(gtdt+68,1,4);sum(gtdt,sizeof(gtdt),9);assert(parse_interrupts(madt,gtdt,&irq) && irq.flags[1]==1);
    gtdt[9]^=1;assert(!parse_interrupts(madt,gtdt,&irq));gtdt[9]^=1;
    memcpy(madt+84,madt+68,16);put(madt+4,100,4);sum(madt,100,9);
    assert(!parse_interrupts(madt,gtdt,&irq));
    uint8_t irq_xsdt[60]={0};
    memcpy(irq_xsdt,"XSDT",4);put(irq_xsdt+4,sizeof(irq_xsdt),4);
    put(irq_xsdt+36,(uintptr_t)mcfg,8);put(irq_xsdt+44,(uintptr_t)madt,8);
    put(irq_xsdt+52,(uintptr_t)gtdt,8);
    put(madt+4,84,4);sum(madt,84,9);sum(irq_xsdt,sizeof(irq_xsdt),9);
    put(rsdp+24,(uintptr_t)irq_xsdt,8);sum(rsdp,36,32);
    assert(find_interrupts((uintptr_t)rsdp,&irq));
    put(irq_xsdt+36,(uintptr_t)gtdt,8);sum(irq_xsdt,sizeof(irq_xsdt),9);
    assert(!find_interrupts((uintptr_t)rsdp,&irq));
    put(irq_xsdt+36,(uintptr_t)mcfg,8);put(irq_xsdt+44,(uintptr_t)mcfg,8);
    sum(irq_xsdt,sizeof(irq_xsdt),9);assert(!find_interrupts((uintptr_t)rsdp,&irq));
    free(ecam);
    puts("vz-platform result=PASS groups=ACPI-PCI,IRQ-bindings,FDT");
}
