// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include "uefi.h"
#include "allocation_tracker.h"
#include <aros/apple/bootinfo.h>
#include "vm-console.h"
#include <libfdt.h>

/* Optional ESP session policy: absent means the bounded boot proof. Values
 * are decimal seconds 1..3600 or "forever", each terminated by one newline.
 * Keep firmware policy separate from the portable kernel's chosen contract. */
static int session_policy(char *text, size_t bytes) {
    if(bytes == 8 && text[0]=='f' && text[1]=='o' && text[2]=='r' &&
       text[3]=='e' && text[4]=='v' && text[5]=='e' && text[6]=='r' && text[7]=='\n') {
        text[7]=0;return 1;
    }
    if(bytes<2 || bytes>5 || text[bytes-1]!='\n' || text[0]=='0')return 0;
    unsigned value=0;
    for(size_t i=0;i<bytes-1;++i) {
        if(text[i]<'0' || text[i]>'9')return 0;
        value=value*10+(unsigned)(text[i]-'0');
    }
    if(value<1 || value>3600)return 0;
    text[bytes-1]=0;return 1;
}
static EFI_STATUS read_policy_file(EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
                                      EFI_CHAR16 *path, char policy[16], int probe) {
    static const struct EFI_GUID loaded_guid={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
    static const struct EFI_GUID fs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
    struct EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
    struct EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    policy[0]=0;
    EFI_STATUS status=system->boot_services->handle_protocol(image,&loaded_guid,(void **)&loaded);
    if(EFI_ERROR(status))return status;
    status=system->boot_services->handle_protocol(loaded->device_handle,&fs_guid,(void **)&fs);
    if(EFI_ERROR(status))return status;
    status=fs->open_volume(fs,&root);
    if(EFI_ERROR(status))return status;
    status=root->open(root,&file,path,1,0);
    root->close(root);
    if(status==EFI_NOT_FOUND)return EFI_SUCCESS;
    if(EFI_ERROR(status))return status;
    EFI_UINTN bytes=16;
    status=file->read(file,&bytes,policy);
    file->close(file);
    if(EFI_ERROR(status))return status;
    if (probe) return bytes==2 && policy[0]=='1' && policy[1]=='\n'?EFI_SUCCESS:EFI_LOAD_ERROR;
    return session_policy(policy,bytes)?EFI_SUCCESS:EFI_LOAD_ERROR;
}

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
static uint64_t find_ecam(struct EFI_SYSTEM_TABLE *system,uint64_t *rsdp_address, uint32_t *last_bus) {
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
                    *last_bus=t[off+11];
                    return little(t+off,8);
                }
            }
        }
    }
    return 0;
}
/* UEFI root bridge Configuration() supplies allocated apertures. Read those
 * before ExitBootServices rather than guessing apertures from endpoint BARs.
 * Protocol pointers/buffers are firmware-owned, as are ACPI table pointers. */
struct VZPCIRange { uint64_t bus, cpu, size; uint32_t flags; };
struct VZPCI {
    uint64_t ecam;
    uint32_t first_bus, last_bus, count;
    struct VZPCIRange ranges[8];
};
struct VZRootBridge {
    void *parent, *poll_mem, *poll_io, *mem_read, *mem_write;
    void *io_read, *io_write, *pci_read, *pci_write, *copy_mem;
    void *map, *unmap, *allocate_buffer, *free_buffer, *flush;
    void *get_attributes, *set_attributes;
    EFI_STATUS (EFIAPI *configuration)(struct VZRootBridge *, void **);
    uint32_t segment;
};
_Static_assert(offsetof(struct VZRootBridge,configuration)==136,"root bridge ABI");
_Static_assert(offsetof(struct VZRootBridge,segment)==144,"root bridge segment ABI");
static int parse_pci_resources(const uint8_t *data, size_t bytes, struct VZPCI *out) {
    struct VZPCI value;
    memset(&value,0,sizeof(value));
    unsigned buses=0;
    for(size_t at=0;at<bytes;) {
        if(data[at]==0x79) {
            if(bytes-at<2 || !buses || !value.count) return 0;
            /* A zero end-tag checksum means checksum validation is omitted. */
            if(data[at+1] && !checksum(data,(uint32_t)(at+2))) return 0;
            memcpy(out,&value,sizeof(value));return 1;
        }
        if(bytes-at<46 || data[at]!=0x8a || little(data+at+1,2)!=43) return 0;
        const uint8_t *d=data+at;
        uint64_t bits=little(d+6,8),base=little(d+14,8),limit=little(d+22,8);
        uint64_t translation=little(d+30,8),span=little(d+38,8);
        if(!span || base>limit || span-1!=limit-base || base>UINT64_MAX-span) return 0;
        if(d[3]==2) {
            if(buses++ || translation || base!=0 || limit>255) return 0;
            value.first_bus=(uint32_t)base;value.last_bus=(uint32_t)limit;
        } else if(d[3]==0) {
            /* VZ currently supplies identity memory apertures. Refuse other
             * translations until their direction and DMA contract are bound. */
            if(!base || translation || (d[5]&~7U) || (bits!=32 && bits!=64) || (base&4095) || (span&4095) ||
                (bits==32 && limit>UINT32_MAX) || value.count==8) return 0;
            for(unsigned i=0;i<value.count;++i)
                if(base<value.ranges[i].bus+value.ranges[i].size &&
                    value.ranges[i].bus<base+span) return 0;
            struct VZPCIRange *r=&value.ranges[value.count++];
            r->bus=r->cpu=base;r->size=span;
            r->flags=bits==64?0x03000000:0x02000000;
            if((d[5]&6)==6) r->flags|=0x40000000;
        } else if(d[3]!=1) return 0;
        /* I/O port apertures have no consumer in this VM profile. */
        at+=46;
    }
    return 0;
}
static int find_pci_resources(struct EFI_SYSTEM_TABLE *system, struct VZPCI *out) {
    static const struct EFI_GUID guid={0x2f707ebb,0x4a1a,0x11d4,{0x9a,0x38,0,0x90,0x27,0x3f,0xc1,0x4d}};
    typedef EFI_STATUS(EFIAPI *Locate)(uint32_t,const struct EFI_GUID *,void *,EFI_UINTN *,EFI_HANDLE **);
    typedef EFI_STATUS(EFIAPI *Free)(void *);
    EFI_HANDLE *handles=NULL;EFI_UINTN count=0;
    struct EFI_BOOT_SERVICES *bs=system->boot_services;
    EFI_STATUS status=((Locate)bs->locate_handle_buffer)(2,&guid,NULL,&count,&handles);
    if(EFI_ERROR(status)) return 0;
    struct VZRootBridge *bridge=NULL;
    if(count==1) status=bs->handle_protocol(handles[0],&guid,(void **)&bridge);
    ((Free)bs->free_pool)(handles);
    if(count!=1 || EFI_ERROR(status) || !bridge || bridge->segment || !bridge->configuration) return 0;
    void *resources=NULL;
    status=bridge->configuration(bridge,&resources);
    if(EFI_ERROR(status) || !resources) return 0;
    /* The protocol has no length output. Bound the firmware descriptor walk
     * before passing its exact span to the independently tested parser. */
    const uint8_t *data=resources;
    for(size_t at=0;at<32*46;) {
        if(data[at]==0x79) return parse_pci_resources(data,at+2,out);
        if(data[at]!=0x8a || little(data+at+1,2)!=43) return 0;
        at+=46;
    }
    return 0;
}
/* ACPI 6.6 sections 5.2.12 and 5.2.25. Bootstrap supports one GICv3
 * redistributor region and the non-secure physical/virtual timer pair. */
struct VZInterrupts {
    uint64_t distributor, redistributors, msi_frame;
    uint32_t msi_flags, msi_base, msi_count;
    uint32_t redistributor_bytes, timer[2], flags[2];
    unsigned cpu_count, psci_hvc, probe;
    uint64_t mpidr[64];
};
/* ACPI 6.6 5.2.9 / 5.2.12.14: FADT ARM_BOOT_ARCH and enabled GICC
 * affinities. Keep the complete Aff3:Aff0 identity, not a low-byte CPU index.
 * https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html */
static int parse_cpus(const uint8_t *madt, const uint8_t *fadt, struct VZInterrupts *out)
{
    if (!madt || !fadt || !signature(madt,"APIC",4) || !signature(fadt,"FACP",4) ||
        table_size(madt)<44 || table_size(fadt)<131) return 0;
    unsigned boot=(unsigned)little(fadt+129,2);
    if (!(boot&1) || (boot&~3U)) return 0;
    out->cpu_count=0;out->psci_hvc=(boot>>1)&1;
    uint32_t size=table_size(madt);
    for (uint32_t at=44;at<size;) {
        if (size-at<2) return 0;
        unsigned length=madt[at+1];
        if (length<2 || length>size-at) return 0;
        if (madt[at]==11) {
            if (length<80) return 0;
            if (little(madt+at+12,4)&1) {
                uint64_t mpidr=little(madt+at+68,8);
                if (mpidr&~UINT64_C(0xff00ffffff) || out->cpu_count==64) return 0;
                for (unsigned i=0;i<out->cpu_count;i++)
                    if (out->mpidr[i]==mpidr) return 0;
                out->mpidr[out->cpu_count++]=mpidr;
            }
        }
        at+=length;
    }
    return out->cpu_count!=0;
}
static int parse_interrupts(const uint8_t *madt, const uint8_t *gtdt,
    struct VZInterrupts *out) {
    struct VZInterrupts value;
    /* EFI has no C runtime; prevent aggregate zeroing from emitting memset. */
    volatile uint8_t *zero=(volatile uint8_t *)&value;
    for (unsigned i=0;i<sizeof(value);i++) zero[i]=0;
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
        } else if(type==13) {
            /* One non-secure MSI frame for this single-root VM profile.
             * With flags bit 0 clear the count/base fields are NOT valid:
             * retain no override, so the driver must read MSI_TYPER. */
            if(length!=24 || value.msi_frame || little(entry+2,2)) return 0;
            value.msi_frame=little(entry+8,8);
            value.msi_flags=(uint32_t)little(entry+16,4);
            if(!value.msi_frame || (value.msi_frame&4095) ||
                value.msi_frame>UINT64_MAX-4096 || (value.msi_flags&~1U)) return 0;
            if(value.msi_flags&1) {
                value.msi_count=(uint32_t)little(entry+20,2);
                value.msi_base=(uint32_t)little(entry+22,2);
                if(value.msi_base<32 || !value.msi_count ||
                    value.msi_base+value.msi_count>1020) return 0;
            }
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
    if(value.msi_frame &&
        ((value.msi_frame<value.distributor+65536 && value.distributor<value.msi_frame+4096) ||
         (value.msi_frame<value.redistributors+value.redistributor_bytes &&
          value.redistributors<value.msi_frame+4096))) return 0;
    for(unsigned i=0;i<2;++i) {
        value.timer[i]=(uint32_t)little(gtdt+56+8*i,4);
        uint32_t flags=(uint32_t)little(gtdt+60+8*i,4);
        /* GICv3 PPIs have fixed active-high polarity. Preserve edge/level;
         * reject unsupported polarity and reserved flags instead of guessing. */
        if(value.timer[i]<16 || value.timer[i]>31 || (flags&~5U)) return 0;
        value.flags[i]=(flags&1)?1:4;
    }
    if(value.timer[0]==value.timer[1]) return 0;
    volatile uint8_t *destination=(volatile uint8_t *)out;
    for (unsigned i=0;i<sizeof(value);i++) destination[i]=zero[i];
    return 1;
}
static int find_interrupts(uint64_t rsdp, struct VZInterrupts *out) {
    const uint8_t *r=(const uint8_t *)(uintptr_t)rsdp;
    if(!r || !signature(r,"RSD PTR ",8) || r[15]<2 || little(r+20,4)!=36 ||
        !checksum(r,20) || !checksum(r,36)) return 0;
    const uint8_t *xsdt=(const uint8_t *)(uintptr_t)little(r+24,8);
    if(!xsdt || !signature(xsdt,"XSDT",4)) return 0;
    uint32_t size=table_size(xsdt);
    const uint8_t *madt=NULL,*gtdt=NULL,*fadt=NULL;
    if(!size || (size-36)%8) return 0;
    for(uint32_t at=36;at<size;at+=8) {
        const uint8_t *t=(const uint8_t *)(uintptr_t)little(xsdt+at,8);
        if(!t) return 0;
        if(signature(t,"APIC",4)) { if(madt) return 0; madt=t; }
        if(signature(t,"GTDT",4)) { if(gtdt) return 0; gtdt=t; }
        if(signature(t,"FACP",4)) { if(fadt) return 0; fadt=t; }
    }
    return parse_interrupts(madt,gtdt,out) && parse_cpus(madt,fadt,out);
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
static int make_tree(void *memory, int capacity, const struct VMConsole *console, uint64_t rsdp, const struct VZInterrupts *irq, const struct VZPCI *pci, const char *session)
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
    if(session && session[0]) {
        if(session[0]=='f')
            FDT_STEP(fdt_property_string(memory,"aros,desktop-hold-forever","1"));
        else
            FDT_STEP(fdt_property_string(memory,"aros,desktop-hold-seconds",session));
    }
    if (irq && irq->probe) FDT_STEP(fdt_property_string(memory,"aros,smp-probe","1"));
    FDT_STEP(fdt_end_node(memory));
    if (irq && irq->cpu_count) {
        FDT_STEP(fdt_begin_node(memory,"cpus"));
        FDT_STEP(fdt_property_u32(memory,"#address-cells",2));
        FDT_STEP(fdt_property_u32(memory,"#size-cells",0));
        for (unsigned i=0;i<irq->cpu_count;i++) {
            char name[21]="cpu@";
            unsigned n=4;int started=0;
            for (int shift=60;shift>=0;shift-=4) {
                unsigned digit=(unsigned)(irq->mpidr[i]>>shift)&15;
                if (digit || started || !shift) {
                    name[n++]="0123456789abcdef"[digit];started=1;
                }
            }
            name[n]=0;
            FDT_STEP(fdt_begin_node(memory,name));
            FDT_STEP(fdt_property_string(memory,"device_type","cpu"));
            FDT_STEP(fdt_property_string(memory,"compatible","arm,armv8"));
            FDT_STEP(fdt_property_string(memory,"enable-method","psci"));
            FDT_STEP(fdt_property_u64(memory,"reg",irq->mpidr[i]));
            FDT_STEP(fdt_end_node(memory));
        }
        FDT_STEP(fdt_end_node(memory));
        FDT_STEP(fdt_begin_node(memory,"psci"));
        FDT_STEP(fdt_property_string(memory,"compatible","arm,psci-0.2"));
        FDT_STEP(fdt_property_string(memory,"method",irq->psci_hvc?"hvc":"smc"));
        FDT_STEP(fdt_end_node(memory));
    }
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
        if(irq->msi_frame) {
            char name[21]="v2m@";
            unsigned n=4,started=0;
            for(int shift=60;shift>=0;shift-=4) {
                unsigned digit=(unsigned)(irq->msi_frame>>shift)&15;
                if(digit || started || !shift) { name[n++]="0123456789abcdef"[digit];started=1; }
            }
            name[n]=0;
            fdt64_t frame[2]={cpu_to_fdt64(irq->msi_frame),cpu_to_fdt64(4096)};
            FDT_STEP(fdt_property_u32(memory,"#address-cells",2));
            FDT_STEP(fdt_property_u32(memory,"#size-cells",2));
            FDT_STEP(fdt_property(memory,"ranges",NULL,0));
            FDT_STEP(fdt_begin_node(memory,name));
            FDT_STEP(fdt_property_string(memory,"compatible","arm,gic-v2m-frame"));
            FDT_STEP(fdt_property_u32(memory,"phandle",2));
            FDT_STEP(fdt_property(memory,"msi-controller",NULL,0));
            FDT_STEP(fdt_property(memory,"reg",frame,sizeof(frame)));
            if(irq->msi_flags&1) {
                FDT_STEP(fdt_property_u32(memory,"arm,msi-base-spi",irq->msi_base));
                FDT_STEP(fdt_property_u32(memory,"arm,msi-num-spis",irq->msi_count));
            }
            FDT_STEP(fdt_end_node(memory));
        }
        FDT_STEP(fdt_end_node(memory));
        FDT_STEP(fdt_begin_node(memory,"timer"));
        FDT_STEP(fdt_property_string(memory,"compatible","arm,armv8-timer"));
        FDT_STEP(fdt_property_u32(memory,"interrupt-parent",1));
        FDT_STEP(fdt_property(memory,"interrupts",interrupts,sizeof(interrupts)));
        FDT_STEP(fdt_property(memory,"interrupt-names","phys\0virt",10));
        FDT_STEP(fdt_end_node(memory));
    }
    if(pci) {
        fdt64_t config[2]={cpu_to_fdt64(pci->ecam),
            cpu_to_fdt64(((uint64_t)pci->last_bus+1)<<20)};
        fdt32_t buses[2]={cpu_to_fdt32(pci->first_bus),cpu_to_fdt32(pci->last_bus)};
        fdt32_t ranges[8*7];
        for(unsigned i=0;i<pci->count;++i) {
            const struct VZPCIRange *r=&pci->ranges[i];
            ranges[7*i]=cpu_to_fdt32(r->flags);
            ranges[7*i+1]=cpu_to_fdt32((uint32_t)(r->bus>>32));
            ranges[7*i+2]=cpu_to_fdt32((uint32_t)r->bus);
            ranges[7*i+3]=cpu_to_fdt32((uint32_t)(r->cpu>>32));
            ranges[7*i+4]=cpu_to_fdt32((uint32_t)r->cpu);
            ranges[7*i+5]=cpu_to_fdt32((uint32_t)(r->size>>32));
            ranges[7*i+6]=cpu_to_fdt32((uint32_t)r->size);
        }
        FDT_STEP(fdt_begin_node(memory,"pcie"));
        FDT_STEP(fdt_property_string(memory,"compatible","pci-host-ecam-generic"));
        FDT_STEP(fdt_property_string(memory,"device_type","pci"));
        FDT_STEP(fdt_property_u32(memory,"#address-cells",3));
        FDT_STEP(fdt_property_u32(memory,"#size-cells",2));
        FDT_STEP(fdt_property_u32(memory,"linux,pci-domain",0));
        /* No ITS/device-ID routing is inferred from an unrelated MADT entry.
         * This profile only associates its sole root with its sole MSI frame. */
        if(irq && irq->msi_frame) FDT_STEP(fdt_property_u32(memory,"msi-parent",2));
        FDT_STEP(fdt_property(memory,"reg",config,sizeof(config)));
        FDT_STEP(fdt_property(memory,"bus-range",buses,sizeof(buses)));
        FDT_STEP(fdt_property(memory,"ranges",ranges,(int)pci->count*28));
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
EFI_STATUS arosboot_vz_platform(EFI_HANDLE image,struct EFI_SYSTEM_TABLE *system,struct AROSBootInfo *info) {
    char session[16],probe[16];
    EFI_CHAR16 session_path[]={'A','R','O','S','\\','S','E','S','S','I','O','N','.','C','F','G',0};
    EFI_CHAR16 probe_path[]={'A','R','O','S','\\','C','P','U','P','R','O','B','E','.','C','F','G',0};
    EFI_STATUS policy_status=read_policy_file(image,system,session_path,session,0);
    if(EFI_ERROR(policy_status))return policy_status;
    policy_status=read_policy_file(image,system,probe_path,probe,1);
    if(EFI_ERROR(policy_status))return policy_status;
    uint32_t ecam_last=0;
    uint64_t rsdp=0,ecam=find_ecam(system,&rsdp,&ecam_last);
    struct VZInterrupts interrupts;
    struct VZPCI pci;
    if(!ecam || (ecam&0xfffff) || !find_interrupts(rsdp,&interrupts) ||
        !find_pci_resources(system,&pci) || pci.last_bus>ecam_last ||
        ecam>UINT64_MAX-(((uint64_t)pci.last_bus+1)<<20)) return EFI_NOT_FOUND;
    interrupts.probe=probe[0]=='1';
    if (interrupts.probe && interrupts.cpu_count<2) return EFI_LOAD_ERROR;
    pci.ecam=ecam;
    uint64_t ecam_end=ecam+(((uint64_t)pci.last_bus+1)<<20);
    if(interrupts.msi_frame && interrupts.msi_frame<ecam_end &&
        ecam<interrupts.msi_frame+4096) return EFI_LOAD_ERROR;
    for(unsigned i=0;i<pci.count;++i) {
        const struct VZPCIRange *r=&pci.ranges[i];
        if((r->cpu<ecam_end && ecam<r->cpu+r->size) ||
           (r->cpu<interrupts.distributor+65536 && interrupts.distributor<r->cpu+r->size) ||
           (r->cpu<interrupts.redistributors+interrupts.redistributor_bytes &&
            interrupts.redistributors<r->cpu+r->size) ||
           (interrupts.msi_frame && r->cpu<interrupts.msi_frame+4096 &&
            interrupts.msi_frame<r->cpu+r->size)) return EFI_LOAD_ERROR;
    }
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
    status=arosboot_allocate_owned(system->boot_services,16384,4096,
        AROS_BOOT_ALLOCATION_FDT,AROS_BOOT_ALLOCATION_FLAG_OWNED|AROS_BOOT_ALLOCATION_FLAG_RETAIN,
        AROS_BOOT_ALLOCATION_NO_MODULE,&memory);
    if(EFI_ERROR(status)) {
        arosboot_free_owned(system->boot_services,console);
        return status;
    }
    system->boot_services->set_mem(memory,16384,0);
    if(make_tree(memory,16384,console,rsdp,&interrupts,&pci,session)) {
        arosboot_free_owned(system->boot_services,memory);
        arosboot_free_owned(system->boot_services,console);
        return EFI_LOAD_ERROR;
    }
    info->fdt_physical=(uintptr_t)memory;info->fdt_size=16384;
    info->flags|=AROS_BOOTINFO_FLAG_FDT;
    /* The shared DMA record must reach RAM before the firmware MMU is retired. */
    for(uintptr_t p=(uintptr_t)console;p<(uintptr_t)console+4096;p+=16)
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    return EFI_SUCCESS;
}
