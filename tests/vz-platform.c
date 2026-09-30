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
    free(ecam);
    puts("vz-platform result=PASS cases=9");
}
