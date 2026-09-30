// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include "uefi.h"
#include "allocation_tracker.h"
#include <aros/apple/bootinfo.h>
#include "vm-console.h"

static uint64_t little(const uint8_t *p,unsigned n) {
    uint64_t v=0;for(unsigned i=0;i<n;++i) v|=(uint64_t)p[i]<<(i*8);return v;
}
static int checksum(const uint8_t *p,uint32_t n) {
    uint8_t sum=0;for(uint32_t i=0;i<n;++i) sum+=p[i];return sum==0;
}
static int signature(const uint8_t *p,const char *s,unsigned n) {
    for(unsigned i=0;i<n;++i) if(p[i]!=(uint8_t)s[i]) return 0;return 1;
}
static uint32_t table_size(const uint8_t *p) {
    uint32_t n=(uint32_t)little(p+4,4);
    return n>=36 && n<=65536 && checksum(p,n)?n:0;
}
static uint64_t find_ecam(struct EFI_SYSTEM_TABLE *system,uint64_t *rsdp_address) {
    static const struct EFI_GUID guid={0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
    for(EFI_UINTN i=0;i<system->number_of_table_entries;++i) {
        struct EFI_CONFIGURATION_TABLE *e=&system->configuration_table[i];
        const uint8_t *a=(const uint8_t *)&e->vendor_guid,*b=(const uint8_t *)&guid;
        unsigned j=0;for(;j<16 && a[j]==b[j];++j) {}
        if(j!=16) continue;
        const uint8_t *r=e->vendor_table;
        if(!r || !signature(r,"RSD PTR ",8) || r[15]<2 ||
            little(r+20,4)!=36 || !checksum(r,20) || !checksum(r,36)) return 0;
        const uint8_t *xsdt=(const uint8_t *)(uintptr_t)little(r+24,8);
        if(!xsdt || !signature(xsdt,"XSDT",4)) return 0;
        uint32_t size=table_size(xsdt);
        if(!size || (size-36)%8) return 0;
        for(uint32_t at=36;at<size;at+=8) {
            const uint8_t *t=(const uint8_t *)(uintptr_t)little(xsdt+at,8);
            if(!t || !signature(t,"MCFG",4)) continue;
            uint32_t n=table_size(t);
            if(n<60 || (n-44)%16) return 0;
            for(uint32_t off=44;off<n;off+=16) {
                if(little(t+off+8,2)==0 && t[off+10]==0) {
                    *rsdp_address=(uintptr_t)r;
                    return little(t+off,8);
                }
            }
        }
    }
    return 0;
}
static int find_console(uint64_t ecam,struct VMConsole *state) {
    for(unsigned dev=0;dev<32;++dev) {
        volatile uint8_t *cfg=(volatile uint8_t *)(uintptr_t)(ecam+((uint64_t)dev<<15));
        if(*(volatile uint32_t *)cfg!=0x10431af4) continue;
        uint64_t common=0,notify=0,start=UINT64_MAX,end=0;
        uint32_t multiplier=0,notify_length=0;
        unsigned cap=cfg[0x34];
        for(unsigned count=0;cap && count<64;++count) {
            if(cap<0x40 || cap>0xf0 || (cap&3)) return 0;
            unsigned length=cfg[cap+2];
            if(cfg[cap]==9 && length>=16) {
                unsigned bar=cfg[cap+4],kind=cfg[cap+3];
                if(kind<1 || kind>4) { cap=cfg[cap+1];continue; }
                if(bar>5 || cap+length>256) return 0;
                uint32_t raw=*(volatile uint32_t *)(cfg+16+bar*4);
                if(raw&1) return 0;
                uint64_t base=raw&~15U;
                if((raw&6)==4) {
                    if(bar==5) return 0;
                    base|=(uint64_t)*(volatile uint32_t *)(cfg+20+bar*4)<<32;
                }
                uint32_t offset=*(volatile uint32_t *)(cfg+cap+8);
                uint32_t bytes=*(volatile uint32_t *)(cfg+cap+12);
                if(!base || !bytes || base>UINT64_MAX-offset-bytes) return 0;
                uint64_t region=base+offset;
                if(region<start) start=region;
                if(region+bytes>end) end=region+bytes;
                if(kind==1) { if(bytes<56 || common) return 0;common=region; }
                if(kind==2) {
                    if(length<20 || notify) return 0;
                    notify=region;notify_length=bytes;
                    multiplier=*(volatile uint32_t *)(cfg+cap+16);
                }
            }
            cap=cfg[cap+1];
        }
        if(cap || !common || !notify || end>UINT64_MAX-4095) return 0;
        state->magic=VM_CONSOLE_MAGIC;state->common=common;state->notify=notify;
        state->multiplier=multiplier;state->notify_length=notify_length;
        state->region_start=start&~UINT64_C(4095);state->region_end=(end+4095)&~UINT64_C(4095);
        /* The guest owns bus mastering after ExitBootServices. */
        *(volatile uint16_t *)(cfg+4)|=6;
        return 1;
    }
    return 0;
}
struct FDTWriter { uint8_t *data;uint32_t at,strings; };
static void be32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(24-i*8)); }
static void cell(struct FDTWriter *w,uint32_t v) { be32(w->data+w->at,v);w->at+=4; }
static unsigned text_size(const char *s) { unsigned n=0;while(s[n]) ++n;return n+1; }
static void node(struct FDTWriter *w,const char *s) {
    cell(w,1);unsigned n=text_size(s);for(unsigned i=0;i<n;++i) w->data[w->at++]=s[i];w->at=(w->at+3)&~3U;
}
static void prop(struct FDTWriter *w,const char *name,const void *data,uint32_t n) {
    uint32_t offset=w->strings;unsigned namesize=text_size(name);
    for(unsigned i=0;i<namesize;++i) w->data[2048+w->strings++]=(uint8_t)name[i];
    cell(w,3);cell(w,n);cell(w,offset);
    for(uint32_t i=0;i<n;++i) w->data[w->at++]=((const uint8_t *)data)[i];w->at=(w->at+3)&~3U;
}
static void pair(uint8_t *p,uint64_t v) { be32(p,(uint32_t)(v>>32));be32(p+4,(uint32_t)v); }
EFI_STATUS arosboot_vz_platform(struct EFI_SYSTEM_TABLE *system,struct AROSBootInfo *info) {
    uint64_t rsdp=0,ecam=find_ecam(system,&rsdp);
    if(!ecam || (ecam&0xfffff)) return EFI_NOT_FOUND;
    struct VMConsole *console=NULL;void *memory=NULL;
    EFI_STATUS status=arosboot_allocate_owned(system->boot_services,4096,4096,
        AROS_BOOT_ALLOCATION_EARLY_CONSOLE,AROS_BOOT_ALLOCATION_FLAG_OWNED|AROS_BOOT_ALLOCATION_FLAG_RETAIN,
        AROS_BOOT_ALLOCATION_NO_MODULE,(void **)&console);
    if(EFI_ERROR(status)) return status;
    system->boot_services->set_mem(console,4096,0);
    if(!find_console(ecam,console)) {
        arosboot_free_owned(system->boot_services,console);
        return EFI_NOT_FOUND;
    }
    status=arosboot_allocate_owned(system->boot_services,4096,4096,
        AROS_BOOT_ALLOCATION_FDT,AROS_BOOT_ALLOCATION_FLAG_OWNED|AROS_BOOT_ALLOCATION_FLAG_RETAIN,
        AROS_BOOT_ALLOCATION_NO_MODULE,&memory);
    if(EFI_ERROR(status)) {
        arosboot_free_owned(system->boot_services,console);
        return status;
    }
    system->boot_services->set_mem(memory,4096,0);
    struct FDTWriter w={(uint8_t *)memory,56,0};uint8_t cells[16];
    node(&w,"");prop(&w,"compatible","aros,virtualization",text_size("aros,virtualization"));
    be32(cells,2);prop(&w,"#address-cells",cells,4);prop(&w,"#size-cells",cells,4);
    pair(cells,(uintptr_t)console);prop(&w,"aros,vm-console-state",cells,8);
    pair(cells,rsdp);prop(&w,"aros,acpi-rsdp",cells,8);
    node(&w,"console");prop(&w,"compatible","aros,virtio-console",text_size("aros,virtio-console"));
    pair(cells,console->region_start);pair(cells+8,console->region_end-console->region_start);
    prop(&w,"reg",cells,16);cell(&w,2);cell(&w,2);cell(&w,9);
    be32(w.data,0xd00dfeed);be32(w.data+4,4096);be32(w.data+8,56);
    be32(w.data+12,2048);be32(w.data+16,40);be32(w.data+20,17);be32(w.data+24,16);
    be32(w.data+32,w.strings);be32(w.data+36,w.at-56);
    info->fdt_physical=(uintptr_t)memory;info->fdt_size=4096;
    info->flags|=AROS_BOOTINFO_FLAG_FDT;
    /* The shared DMA record must reach RAM before the firmware MMU is retired. */
    for(uintptr_t p=(uintptr_t)console;p<(uintptr_t)console+4096;p+=16)
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    return EFI_SUCCESS;
}
