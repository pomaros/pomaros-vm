// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include "uefi.h"
#include "allocation_tracker.h"
#include <aros/apple/bootinfo.h>
#include "vm-console.h"
#include <libfdt.h>

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
/* ACPI 6.6 sections 5.2.12 and 5.2.25. Bootstrap supports one GICv3
 * redistributor region and the non-secure physical/virtual timer pair. */
struct VZInterrupts {
    uint64_t distributor, redistributors;
    uint32_t redistributor_bytes, timer[2], flags[2];
};
static int parse_interrupts(const uint8_t *madt, const uint8_t *gtdt,
    struct VZInterrupts *out) {
    struct VZInterrupts value={0};
    if(!madt || !gtdt || !signature(madt,"APIC",4) || !signature(gtdt,"GTDT",4)) return 0;
    uint32_t size=table_size(madt), timer_size=table_size(gtdt);
    if(size<44 || timer_size<96) return 0;
    for(uint32_t at=44;at<size;) {
        if(size-at<2) return 0;
        unsigned length=madt[at+1],type=madt[at];
        const uint8_t *entry=madt+at;
        if(length<2 || length>size-at) return 0;
        if(type==12) {
            if(length<24 || value.distributor || entry[20]!=3 || little(entry+16,4)) return 0;
            value.distributor=little(entry+8,8);
            if(!value.distributor || (value.distributor&65535) || value.distributor>UINT64_MAX-65536) return 0;
        } else if(type==14) {
            if(length<16 || value.redistributors || little(entry+2,2)) return 0;
            value.redistributors=little(entry+4,8);
            value.redistributor_bytes=(uint32_t)little(entry+12,4);
            if(!value.redistributors || (value.redistributors&65535) ||
                value.redistributor_bytes<0x20000 || (value.redistributor_bytes&65535) ||
                value.redistributors>UINT64_MAX-value.redistributor_bytes) return 0;
        }
        at+=length;
    }
    if(!value.distributor || !value.redistributors ||
        (value.distributor<value.redistributors+value.redistributor_bytes &&
         value.redistributors<value.distributor+65536)) return 0;
    for(unsigned i=0;i<2;++i) {
        value.timer[i]=(uint32_t)little(gtdt+56+8*i,4);
        uint32_t flags=(uint32_t)little(gtdt+60+8*i,4);
        /* GICv3 PPIs have fixed active-high polarity. Preserve edge/level;
         * reject unsupported polarity and reserved flags instead of guessing. */
        if(value.timer[i]<16 || value.timer[i]>31 || (flags&~5U)) return 0;
        value.flags[i]=(flags&1)?1:4;
    }
    if(value.timer[0]==value.timer[1]) return 0;
    *out=value;
    return 1;
}
static int find_interrupts(uint64_t rsdp, struct VZInterrupts *out) {
    const uint8_t *r=(const uint8_t *)(uintptr_t)rsdp;
    if(!r || !signature(r,"RSD PTR ",8) || r[15]<2 || little(r+20,4)!=36 ||
        !checksum(r,20) || !checksum(r,36)) return 0;
    const uint8_t *xsdt=(const uint8_t *)(uintptr_t)little(r+24,8);
    if(!xsdt || !signature(xsdt,"XSDT",4)) return 0;
    uint32_t size=table_size(xsdt);
    const uint8_t *madt=NULL,*gtdt=NULL;
    if(!size || (size-36)%8) return 0;
    for(uint32_t at=36;at<size;at+=8) {
        const uint8_t *t=(const uint8_t *)(uintptr_t)little(xsdt+at,8);
        if(!t) return 0;
        if(signature(t,"APIC",4)) { if(madt) return 0; madt=t; }
        if(signature(t,"GTDT",4)) { if(gtdt) return 0; gtdt=t; }
    }
    return parse_interrupts(madt,gtdt,out);
}
static int find_console(uint64_t ecam,struct VMConsole *state) {
    /* VZ exposes one port-zero console as a function-zero PCI endpoint.
     * Select the first match; general PCI enumeration belongs to its bus driver. */
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
/* Bounded sequential writer shared with the reference FDT implementation.
 * Firmware discovery ends here; the kernel consumes the resulting FDT. */
static int make_tree(void *memory, int capacity, const struct VMConsole *console, uint64_t rsdp, const struct VZInterrupts *irq)
{
    fdt64_t reg[2]={cpu_to_fdt64(console->region_start),
        cpu_to_fdt64(console->region_end-console->region_start)};
#define FDT_STEP(call) do { int error=(call); if(error) return error; } while(0)
    FDT_STEP(fdt_create(memory,capacity));
    FDT_STEP(fdt_finish_reservemap(memory));
    FDT_STEP(fdt_begin_node(memory,""));
    FDT_STEP(fdt_property_string(memory,"compatible","aros,virtualization"));
    FDT_STEP(fdt_property_u32(memory,"#address-cells",2));
    FDT_STEP(fdt_property_u32(memory,"#size-cells",2));
    FDT_STEP(fdt_property_u64(memory,"aros,vm-console-state",(uintptr_t)console));
    FDT_STEP(fdt_property_u64(memory,"aros,acpi-rsdp",rsdp));
    FDT_STEP(fdt_begin_node(memory,"chosen"));
    FDT_STEP(fdt_property_string(memory,"stdout-path","/console"));
    FDT_STEP(fdt_end_node(memory));
    if(irq) {
        fdt64_t windows[4]={cpu_to_fdt64(irq->distributor),cpu_to_fdt64(65536),
            cpu_to_fdt64(irq->redistributors),cpu_to_fdt64(irq->redistributor_bytes)};
        fdt32_t interrupts[6];
        for(unsigned i=0;i<2;++i) {
            interrupts[3*i]=cpu_to_fdt32(1);
            interrupts[3*i+1]=cpu_to_fdt32(irq->timer[i]-16);
            interrupts[3*i+2]=cpu_to_fdt32(irq->flags[i]);
        }
        FDT_STEP(fdt_begin_node(memory,"interrupt-controller"));
        FDT_STEP(fdt_property_string(memory,"compatible","arm,gic-v3"));
        FDT_STEP(fdt_property_u32(memory,"phandle",1));
        FDT_STEP(fdt_property_u32(memory,"#interrupt-cells",3));
        FDT_STEP(fdt_property(memory,"interrupt-controller",NULL,0));
        FDT_STEP(fdt_property(memory,"reg",windows,sizeof(windows)));
        FDT_STEP(fdt_end_node(memory));
        FDT_STEP(fdt_begin_node(memory,"timer"));
        FDT_STEP(fdt_property_string(memory,"compatible","arm,armv8-timer"));
        FDT_STEP(fdt_property_u32(memory,"interrupt-parent",1));
        FDT_STEP(fdt_property(memory,"interrupts",interrupts,sizeof(interrupts)));
        FDT_STEP(fdt_property(memory,"interrupt-names","phys\0virt",10));
        FDT_STEP(fdt_end_node(memory));
    }
    FDT_STEP(fdt_begin_node(memory,"console"));
    FDT_STEP(fdt_property_string(memory,"compatible","aros,virtio-console"));
    FDT_STEP(fdt_property(memory,"reg",reg,sizeof(reg)));
    FDT_STEP(fdt_end_node(memory));
    FDT_STEP(fdt_end_node(memory));
    FDT_STEP(fdt_finish(memory));
    /* The retained allocation's used size is the complete zero-filled page. */
    fdt_set_totalsize(memory,(uint32_t)capacity);
    return fdt_check_full(memory,(size_t)capacity);
#undef FDT_STEP
}
EFI_STATUS arosboot_vz_platform(struct EFI_SYSTEM_TABLE *system,struct AROSBootInfo *info) {
    uint64_t rsdp=0,ecam=find_ecam(system,&rsdp);
    struct VZInterrupts interrupts;
    if(!ecam || (ecam&0xfffff) || !find_interrupts(rsdp,&interrupts)) return EFI_NOT_FOUND;
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
    if(make_tree(memory,4096,console,rsdp,&interrupts)) {
        arosboot_free_owned(system->boot_services,memory);
        arosboot_free_owned(system->boot_services,console);
        return EFI_LOAD_ERROR;
    }
    info->fdt_physical=(uintptr_t)memory;info->fdt_size=4096;
    info->flags|=AROS_BOOTINFO_FLAG_FDT;
    /* The shared DMA record must reach RAM before the firmware MMU is retired. */
    for(uintptr_t p=(uintptr_t)console;p<(uintptr_t)console+4096;p+=16)
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    return EFI_SUCCESS;
}
