// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/openfirmware.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include <aros/asmcall.h>
#include <interface/HW.h>
#include "ecam/pciecam.h"
#include "pci_mapping.h"
#include "status.h"
#include <string.h>
#include <hardware/virtio.h>
#undef OOPBase
#undef UtilityBase
struct Library *OOPBase;
int __nowbsupport=1;
int __nostdiowin=1;
struct WBStartup *WBenchMsg;
APTR OpenFirmwareBase;
APTR KernelBase;
static OOP_AttrBase device_attr;
static unsigned enumerated,console_seen,expected,mapped_common,mapping_failed;
static int check_mappings,check_resident;
static unsigned resident_checked,dma_checked,dma_failed,block_checked;
int test_block_queue(OOP_Object *,struct Library *,int);
int test_virtio_device(void);
int test_gpu_query(OOP_Object *,struct Library *);
static OOP_Object *driver;
static struct PCIECAMConfig host;
static OOP_MethodID read_base;
static void mark(const char *s) { while(*s) RawPutChar(*s++); }
static void hex(const char *name,uint64_t value)
{
    mark(name);mark("=0x");
    for(int shift=60;shift>=0;shift-=4) RawPutChar("0123456789abcdef"[(value>>shift)&15]);
    mark("\n");
}
static uint32_t cell(const UBYTE *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void complete_write(void) { __asm__ volatile("dsb sy" ::: "memory"); }
/* Read-only validation of the stage2-admitted frame. The guest profile has
 * one PCI root and at most one associated v2m frame. This is not an IRQ test. */
static uintptr_t msi_frame;
static uint32_t msi_typer;
static IPTR read_msi_frame(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
    msi_typer=*(volatile uint32_t *)(msi_frame+8);
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}
static int check_msi_frame(void *pci)
{
    void *parent=OF_FindProperty(pci,"msi-parent");
    if(!parent) { mark("[VM] PCI MSI frame absent\n");return 1; }
    void *node=OF_FindNodeByCompatible(NULL,"arm,gic-v2m-frame");
    if(!node) { mark("[VM] PCI MSI controller unsupported\n");return 1; }
    void *phandle=OF_FindProperty(node,"phandle"),*reg=OF_FindProperty(node,"reg");
    if(OF_GetPropLen(parent)!=4 || !phandle || OF_GetPropLen(phandle)!=4 ||
        cell(OF_GetPropValue(parent))!=cell(OF_GetPropValue(phandle)) ||
        !reg || OF_GetPropLen(reg)!=16) return 0;
    const UBYTE *r=OF_GetPropValue(reg);
    msi_frame=((uint64_t)cell(r)<<32)|cell(r+4);
    if(!msi_frame || (msi_frame&4095) || cell(r+8) || cell(r+12)!=4096) return 0;
    Supervisor((APTR)read_msi_frame);
    hex("[VM] MSI frame",msi_frame);hex("[VM] MSI_TYPER",msi_typer);
    unsigned base=(msi_typer>>16)&1023,count=msi_typer&1023;
    void *override_base=OF_FindProperty(node,"arm,msi-base-spi");
    void *override_count=OF_FindProperty(node,"arm,msi-num-spis");
    if(!!override_base!=!!override_count) return 0;
    if(override_base) {
        if(OF_GetPropLen(override_base)!=4 || OF_GetPropLen(override_count)!=4) return 0;
        base=cell(OF_GetPropValue(override_base));count=cell(OF_GetPropValue(override_count));
    }
    if(base<32 || base>=1020 || !count || count>1020-base) return 0;
    hex("[VM] MSI SPI base",base);hex("[VM] MSI SPI count",count);
    mark("[VM] MSI frame mapped read PASS\n");
    return 1;
}
static IPTR reserve_console(void)
{
    for(unsigned slot=0;slot<32;++slot) {
        uint32_t id=AROSPCIECAMRead(&host.config,0,slot,0,0,4);
        if(id==0x10431af4) host.reserved_root_slots |= UINT32_C(1)<<slot;
        else if((id&0xffff)==0x1af4) ++expected;
    }
    return 0;
}
static IPTR check_driver(void)
{
    unsigned visible=0,hidden=0;
    for(unsigned slot=0;slot<32;++slot) {
        struct pHidd_PCIDriver_ReadConfigLong msg={read_base+moHidd_PCIDriver_ReadConfigLong,NULL,0,slot,0,0};
        uint32_t id=OOP_DoMethod(driver,(OOP_Msg)&msg);
        if(host.reserved_root_slots & (UINT32_C(1)<<slot)) {
            if(id!=UINT32_MAX) return 0;
            ++hidden;
        } else if((id&0xffff)==0x1af4) ++visible;
    }
    return visible && hidden;
}
static uint32_t config_read(OOP_Object *device,unsigned reg,unsigned width)
{
    OOP_MethodID mb=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,0);
    if(width==2) {
        struct pHidd_PCIDevice_ReadConfigWord msg={mb+moHidd_PCIDevice_ReadConfigWord,reg};
        return OOP_DoMethod(device,(OOP_Msg)&msg);
    }
    if(width==1) {
        struct pHidd_PCIDevice_ReadConfigByte msg={mb+moHidd_PCIDevice_ReadConfigByte,reg};
        return OOP_DoMethod(device,(OOP_Msg)&msg);
    }
    struct pHidd_PCIDevice_ReadConfigLong msg={mb+moHidd_PCIDevice_ReadConfigLong,reg};
    return OOP_DoMethod(device,(OOP_Msg)&msg);
}
static void config_write_word(OOP_Object *device,unsigned reg,uint16_t value)
{
    struct pHidd_PCIDevice_WriteConfigWord msg={
        OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,moHidd_PCIDevice_WriteConfigWord),reg,value};
    OOP_DoMethod(device,(OOP_Msg)&msg);
}
static uint32_t transport_read(void *context,unsigned reg,unsigned width)
{ return config_read(context,reg,width); }
static int transport_bar(void *context,unsigned bar,uint64_t *bus,uint64_t *bytes)
{
    IPTR base=0,size=0;
    for(unsigned i=0;i<=bar;++i) {
        uint32_t raw=config_read(context,0x10+4*i,4);
        if(i==bar && (raw&1)) return 0;
        if(!(raw&1) && (raw&6)==4) { if(i+1==bar) return 0; ++i; }
    }
    OOP_GetAttr(context,device_attr+aoHidd_PCIDevice_Base0+3*bar,&base);
    OOP_GetAttr(context,device_attr+aoHidd_PCIDevice_Size0+3*bar,&size);
    *bus=base&~(IPTR)15;*bytes=size;return *bus && *bytes;
}
static void *transport_map(void *context,uint64_t bus,uint32_t bytes)
{
    IPTR owner=0;OOP_GetAttr(context,device_attr+aoHidd_PCIDevice_Driver,&owner);
    if(!owner || bus>UINTPTR_MAX) return NULL;
    struct pHidd_PCIDriver_MapPCI msg={read_base+moHidd_PCIDriver_MapPCI,(void *)(uintptr_t)bus,bytes};
    return (void *)OOP_DoMethod((void *)owner,(OOP_Msg)&msg);
}
static void transport_unmap(void *context,void *cpu,uint32_t bytes)
{
    IPTR owner=0;OOP_GetAttr(context,device_attr+aoHidd_PCIDevice_Driver,&owner);
    struct pHidd_PCIDriver_UnmapPCI msg={read_base+moHidd_PCIDriver_UnmapPCI,cpu,bytes};
    OOP_DoMethod((void *)owner,(OOP_Msg)&msg);
}
static uint8_t status_read(void *context)
{
    uint8_t value=*(volatile uint8_t *)context;
    complete_write();
    return value;
}
static void status_write(void *context,uint8_t value)
{
    complete_write();
    *(volatile uint8_t *)context=value;
    complete_write();
}
static int map_common(OOP_Object *device)
{
    const struct VirtioPCIAccess io={device,transport_read,transport_bar,transport_map,transport_unmap};
    struct VirtioPCIMapping mapping={0};
    if(!VirtioPCIMap(&io,&mapping,7)) return 0;
    uint16_t command=config_read(device,4,2);
    config_write_word(device,4,command|2);
    unsigned queues=*(volatile uint16_t *)((uint8_t *)mapping.regions[0]+18);
    /* Console is excluded, no native driver owns these test devices, and no
     * queue is submitted. Exercise the shared reset protocol and leave them
     * stopped, never at DRIVER_OK. File-backed guest disks only. */
    const struct VirtioStatusAccess status={
        (uint8_t *)mapping.regions[0]+20,status_read,status_write};
    int began=VirtioStatusBegin(&status,1000000);
    int stopped=VirtioStatusReset(&status,1000000);
    config_write_word(device,4,command);
    hex("[VM] virtio queues",queues);
    VirtioPCIUnmap(&io,&mapping);
    return queues>0 && queues<0x8000 && began && stopped;
}
static void *dma_method(OOP_Object *owner,unsigned method,void *address)
{
    struct pHidd_PCIDriver_CPUtoPCI msg={read_base+method,address};
    return (void *)OOP_DoMethod(owner,(OOP_Msg)&msg);
}
static uint64_t counter(void) { uint64_t n;__asm__ volatile("mrs %0,cntvct_el0":"=r"(n));return n; }
static int entropy_dma(OOP_Object *device)
{
    IPTR owner_value=0;OOP_GetAttr(device,device_attr+aoHidd_PCIDevice_Driver,&owner_value);
    OOP_Object *owner=(void *)owner_value;
    const struct VirtioPCIAccess io={device,transport_read,transport_bar,transport_map,transport_unmap};
    struct VirtioPCIMapping mapping={0};
    if(!owner || !VirtioPCIMap(&io,&mapping,7)) return 0;
    volatile struct virtio_pci_common_cfg *cfg=mapping.regions[0];
    struct pHidd_PCIDriver_AllocPCIMem alloc={read_base+moHidd_PCIDriver_AllocPCIMem,4096};
    uint8_t *cpu=(void *)OOP_DoMethod(owner,(OOP_Msg)&alloc);
    if(!cpu) { VirtioPCIUnmap(&io,&mapping);return 0; }
    uint64_t bus=(uintptr_t)dma_method(owner,moHidd_PCIDriver_CPUtoPCI,cpu);
    int okay=bus && bus!=UINTPTR_MAX &&
        dma_method(owner,moHidd_PCIDriver_PCItoCPU,(void *)(uintptr_t)(bus+4095))==cpu+4095 &&
        dma_method(owner,moHidd_PCIDriver_CPUtoPCI,cpu+4096)==(void *)-1;
    uint16_t command=config_read(device,4,2);config_write_word(device,4,command|6);
    const struct VirtioStatusAccess status={(uint8_t *)cfg+20,status_read,status_write};
    if(!okay || !VirtioStatusBegin(&status,1000000)) { okay=0;goto stop; }
    cfg->device_feature_select=1;complete_write();
    if(!(cfg->device_feature&1)) { okay=0;goto stop; }
    cfg->guest_feature_select=0;cfg->guest_feature=0;complete_write();
    cfg->guest_feature_select=1;cfg->guest_feature=1;complete_write();
    status_write(status.context,11); /* ACK|DRIVER|FEATURES_OK */
    if(status_read(status.context)!=11) { okay=0;goto stop; }
    cfg->queue_select=0;complete_write();
    if(cfg->queue_size<8) { okay=0;goto stop; }
    cfg->queue_size=8;cfg->queue_msix_vector=0xffff;
    cfg->queue_desc=bus;cfg->queue_driver=bus+256;cfg->queue_device=bus+512;
    volatile uint16_t *notify=VirtioPCINotify(&mapping,cfg->queue_notify_off);
    if(!notify) { okay=0;goto stop; }
    volatile struct virtq_desc *desc=(void *)cpu;
    volatile struct virtq_avail *avail=(void *)(cpu+256);
    volatile struct virtq_used *used=(void *)(cpu+512);
    memset(cpu,0,4096);memset(cpu+1024,0xa5,64);
    desc[0].addr=bus+1024;desc[0].len=64;desc[0].flags=VIRTQ_DESC_F_WRITE;
    avail->flags=1; /* Polling probe: request no interrupt. */
    cfg->queue_enable=1;complete_write();status_write(status.context,15);
    if(status_read(status.context)!=15) { okay=0;goto stop; }
    avail->ring[0]=0;complete_write();avail->idx=1;complete_write();*notify=0;complete_write();
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t start=counter();
    while(used->idx==0 && counter()-start<frequency) __asm__ volatile("yield");
    complete_write();
    okay=used->idx==1 && used->ring[0].id==0 && used->ring[0].len>0 && used->ring[0].len<=64;
stop:
    /* Never free a buffer a non-acknowledged device could still modify. */
    if(!VirtioStatusReset(&status,1000000)) return 0;
    config_write_word(device,4,command);
    struct pHidd_PCIDriver_FreePCIMem release={read_base+moHidd_PCIDriver_FreePCIMem,cpu};
    OOP_DoMethod(owner,(OOP_Msg)&release);
    okay=okay && dma_method(owner,moHidd_PCIDriver_CPUtoPCI,cpu)==(void *)-1;
    VirtioPCIUnmap(&io,&mapping);
    return okay;
}
/* Read-only admission of the USB controller exposed by VZ input devices.
 * Decode capability registers only; do not reset, start or arm DMA. */
static int inspect_xhci(OOP_Object *device)
{
    hex("[VM] xHCI legacy interrupt line",config_read(device,0x3c,1));
    uint64_t visited=0;
    unsigned cap=config_read(device,0x34,1);
    for(unsigned steps=0;cap;++steps) {
        if(steps==48 || cap<0x40 || cap>0xfc || (cap&3))return 0;
        uint64_t bit=UINT64_C(1)<<(cap/4);
        if(visited&bit)return 0;
        visited|=bit;
        unsigned id=config_read(device,cap,1);
        hex("[VM] xHCI PCI capability",id);
        if(id==5 || id==0x11)hex("[VM] xHCI interrupt control",config_read(device,cap+2,2));
        cap=config_read(device,cap+1,1);
    }
    uint64_t bus,bytes;
    if(!transport_bar(device,0,&bus,&bytes) || bytes<32 || bytes>(1u<<20))return 0;
    hex("[VM] xHCI BAR0",bus);hex("[VM] xHCI BAR0 bytes",bytes);
    volatile UBYTE *regs=transport_map(device,bus,(uint32_t)bytes);
    if(!regs || regs==(void *)-1)return 0;
    UWORD command=config_read(device,4,2);
    config_write_word(device,4,command|2);
    unsigned length=regs[0],version=*(volatile UWORD *)(regs+2);
    uint32_t structural=*(volatile uint32_t *)(regs+4);
    hex("[VM] xHCI capability length",length);hex("[VM] xHCI version",version);
    hex("[VM] xHCI structural parameters",structural);
    hex("[VM] xHCI capability parameters",*(volatile uint32_t *)(regs+16));
    config_write_word(device,4,command);
    transport_unmap(device,(void *)regs,(uint32_t)bytes);
    return length>=32 && length<bytes && version>=0x100 && (structural>>24)!=0;
}
/* Read configuration only; no network queues or packets are submitted. */
int test_network_packets(OOP_Object *,struct Library *);
static int inspect_network(OOP_Object *device)
{
    const struct VirtioPCIAccess io={device,transport_read,transport_bar,transport_map,transport_unmap};
    struct VirtioPCIMapping mapping={0};
    if(!VirtioPCIMap(&io,&mapping,15))return 0;
    UWORD command=config_read(device,4,2);
    config_write_word(device,4,command|2);
    volatile UBYTE *common=mapping.regions[0],*cfg=mapping.regions[3];
    ULONG selector=*(volatile ULONG *)(common+0);
    *(volatile ULONG *)(common+0)=0;complete_write();
    ULONG features=*(volatile ULONG *)(common+4);
    hex("[VM] virtio network offered low",features);
    *(volatile ULONG *)(common+0)=1;complete_write();
    hex("[VM] virtio network offered high",*(volatile ULONG *)(common+4));
    *(volatile ULONG *)(common+0)=selector;complete_write();
    int okay=0;
    hex("[VM] virtio network config bytes",mapping.bytes[3]);
    if((features&(1u<<5)) && mapping.bytes[3]>=6) {
        for(unsigned attempt=0;attempt<8;++attempt) {
            UBYTE generation=common[21],mac[6];complete_write();
            for(unsigned i=0;i<6;++i)mac[i]=cfg[i];
            complete_write();
            if(common[21]!=generation)continue;
            uint64_t address=0;
            for(unsigned i=0;i<6;++i)address=(address<<8)|mac[i];
            okay=(mac[0]&3)==2 && address!=0;
            if(okay)hex("[VM] virtio network MAC",address);
            break;
        }
    }
    config_write_word(device,4,command);
    VirtioPCIUnmap(&io,&mapping);
    return okay;
}
AROS_UFH3(static IPTR, count_device,
    AROS_UFHA(struct Hook *,hook,A0),
    AROS_UFHA(OOP_Object *,device,A2),
    AROS_UFHA(APTR,message,A1))
{
    AROS_USERFUNC_INIT
    IPTR vendor=0,product=0;
    (void)hook;(void)message;
    OOP_GetAttr(device,device_attr+aoHidd_PCIDevice_VendorID,&vendor);
    OOP_GetAttr(device,device_attr+aoHidd_PCIDevice_ProductID,&product);
    if(check_mappings) {
        hex("[VM] PCI vendor",vendor); hex("[VM] PCI product",product);
        unsigned class_code=config_read(device,8,4)>>8;
        hex("[VM] PCI class",class_code);
        if(class_code==0x0c0330) {
            if(inspect_xhci(device))mark("[VM] xHCI capability mapping PASS\n");
            else ++mapping_failed;
        }
    }
    if(vendor==0x1af4) {
        ++enumerated;
        if(check_resident) {
            uint16_t command=config_read(device,4,2);
            config_write_word(device,4,command);
            if(config_read(device,4,2)==command) ++resident_checked;
            if(product==0x1042 || product==0x1001) {
                if(test_block_queue(device,OOPBase,msi_frame!=0)) ++block_checked;else ++dma_failed;
            }
            if(product==0x1050) {
                if(test_gpu_query(device,OOPBase)) mark("[VM] virtio GPU display query PASS\n");
                else ++dma_failed;
            }
            if(product==0x1041) {
                if(inspect_network(device)) {
                    mark("[VM] virtio network configuration PASS\n");
                    if(!test_network_packets(device,OOPBase))++dma_failed;
                }
                else ++dma_failed;
            }
            if(product==0x1044) {
                if(entropy_dma(device)) ++dma_checked;else ++dma_failed;
            }
        }
        if(check_mappings) hex("[VM] virtio product",product);
        if(product==0x1043) ++console_seen;
        if(check_mappings) { if(map_common(device)) ++mapped_common; else ++mapping_failed; }
    }
    return 0;
    AROS_USERFUNC_EXIT
}
static int register_bridge(struct TagItem *tags)
{
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    if(!KernelBase || host.config.first_bus || host.config.bytes<(1u<<20)) return 0;
    /* This VZ qualification admits only the flat root bus. Other buses and
     * DMA remain unavailable. Stage2 already checked the aperture against RAM. */
    host.config.last_bus=0;host.config.bytes=1u<<20;
    KrnSetProtection((void *)host.config.mapping,host.config.bytes,MAP_Readable|MAP_Writable);
    for(unsigned i=0;i<host.window_count;++i)
        KrnSetProtection((void *)(uintptr_t)host.windows[i].cpu,host.windows[i].bytes,
            MAP_Readable|MAP_Writable);
    device_attr=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    OOP_Object *pci=OOP_NewObject(NULL,(CONST_STRPTR)CLID_Hidd_PCI,NULL);
    if(!device_attr || !pci) return 0;
    struct pHW_AddDriver add={OOP_GetMethodID((CONST_STRPTR)IID_HW,moHW_AddDriver),
        OOP_FindClass((CONST_STRPTR)CLID_Hidd_PCIECAM),tags};
    OOP_Object *registered=(OOP_Object *)OOP_DoMethod(pci,(OOP_Msg)&add);
    if(!registered) return 0;
    struct Hook hook={0};hook.h_Entry=(HOOKFUNC)count_device;
    struct pHidd_PCI_EnumDevices enumerate={
        OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCI,moHidd_PCI_EnumDevices),&hook,NULL};
    check_mappings=1;
    OOP_DoMethod(pci,(OOP_Msg)&enumerate);
    check_mappings=0;
    unsigned before=enumerated;
    struct pHW_RemoveDriver remove={OOP_GetMethodID((CONST_STRPTR)IID_HW,moHW_RemoveDriver),registered};
    if(!OOP_DoMethod(pci,(OOP_Msg)&remove)) return 0;
    enumerated=0;
    OOP_DoMethod(pci,(OOP_Msg)&enumerate);
    OOP_DisposeObject(pci);OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    KrnSetProtection((void *)host.config.mapping,host.config.bytes,MAP_Readable|MAP_Writable|MAP_Supervisor);
    for(unsigned i=0;i<host.window_count;++i)
        KrnSetProtection((void *)(uintptr_t)host.windows[i].cpu,host.windows[i].bytes,
            MAP_Readable|MAP_Writable|MAP_Supervisor);
    return expected && before==expected && mapped_common==expected && !mapping_failed &&
        !enumerated && !console_seen;
}
static int resident_bridge(void)
{
    device_attr=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    OOP_Object *pci=OOP_NewObject(NULL,(CONST_STRPTR)CLID_Hidd_PCI,NULL);
    if(!device_attr || !pci) return 0;
    struct Hook hook={0};hook.h_Entry=(HOOKFUNC)count_device;
    struct pHidd_PCI_EnumDevices enumerate={
        OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCI,moHidd_PCI_EnumDevices),&hook,NULL};
    check_resident=1;
    OOP_DoMethod(pci,(OOP_Msg)&enumerate);
    OOP_DisposeObject(pci);OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    return expected && enumerated==expected && resident_checked==expected && !console_seen && dma_checked==1 && block_checked==1 && !dma_failed;
}
int test_xhci_device(void);
int main(int argc,char **argv)
{
    mark("[VM] PCI HIDD load begin\n");
    OOPBase=OpenLibrary((CONST_STRPTR)"oop.library",0);
    if(!OOPBase) { mark("[VM] PCI HIDD fail oop.library\n"); return 81; }
    mark("[VM] PCI HIDD oop loaded\n");
    if(!OpenLibrary((CONST_STRPTR)"DEVS:Drivers/hiddclass.hidd",0)) { mark("[VM] PCI HIDD fail hiddclass\n"); return 82; }
    mark("[VM] PCI HIDD hiddclass loaded\n");
    if(!OpenLibrary((CONST_STRPTR)"DEVS:Drivers/pci.hidd",0)) { mark("[VM] PCI HIDD fail pci\n"); return 83; }
    if(!OpenLibrary((CONST_STRPTR)"DEVS:Drivers/pciecam.hidd",0)) { mark("[VM] PCI HIDD fail pciecam\n"); return 84; }
    mark("[VM] PCI HIDD libraries loaded\n");
    OpenFirmwareBase=OpenResource((CONST_STRPTR)"openfirmware.resource");
    if(!OpenFirmwareBase) return 85;
    void *pci=OF_FindNodeByCompatible(NULL,"pci-host-ecam-generic");
    void *reg=pci?OF_FindProperty(pci,"reg"):NULL;
    void *buses=pci?OF_FindProperty(pci,"bus-range"):NULL;
    if(!reg || OF_GetPropLen(reg)!=16 || !buses || OF_GetPropLen(buses)!=8) return 86;
    const UBYTE *r=OF_GetPropValue(reg),*b=OF_GetPropValue(buses);
    host.config.mapping=(void *)(uintptr_t)(((uint64_t)cell(r)<<32)|cell(r+4));
    host.config.bytes=((uint64_t)cell(r+8)<<32)|cell(r+12);
    host.config.first_bus=cell(b);host.config.last_bus=cell(b+4);
    void *ranges=OF_FindProperty(pci,"ranges");
    if(!ranges) return 86;
    unsigned range_bytes=OF_GetPropLen(ranges);
    if(!range_bytes || range_bytes%28 || range_bytes>PCIECAM_MAX_WINDOWS*28) return 86;
    const UBYTE *range=OF_GetPropValue(ranges);
    for(unsigned at=0;at<range_bytes;at+=28) {
        unsigned space=(cell(range+at)>>24)&3;
        if(space==1) continue;
        if(space!=2 && space!=3) return 86;
        struct PCIECAMWindow *w=&host.windows[host.window_count++];
        w->bus=((uint64_t)cell(range+at+4)<<32)|cell(range+at+8);
        w->cpu=((uint64_t)cell(range+at+12)<<32)|cell(range+at+16);
        w->bytes=((uint64_t)cell(range+at+20)<<32)|cell(range+at+24);
        if(!w->bytes || w->bytes>UINT32_MAX) return 86;
    }
    host.complete_config_write=complete_write;
    Supervisor((APTR)reserve_console);
    if(argc==2 && !strcmp(argv[1],"resident")) {
        read_base=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDriver,0);
        /* Do not load vmpci here: its bridge must survive the prior command. */
        if(!check_msi_frame(pci)) return 93;
        if(!resident_bridge()) return 91;
        mark("[VM] PCI resident bridge after command unload PASS\n");
        mark("[VM] virtio entropy DMA completion PASS\n");
        mark("[VM] virtio block queue sector read PASS\n");
        if(msi_frame) mark("[VM] virtio block MSI-X completion PASS\n");
        int full=test_virtio_device();
        if(!full) return 94;
        if(full>0) mark("[VM] full virtio.device IRQ read PASS\n");
        if(!test_xhci_device())return 95;
        return 0;
    }
    OOP_AttrBase attr=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIECAM);
    if(!attr) return 87;
    struct TagItem tags[]={{attr+aoHidd_PCIECAM_Config,(IPTR)&host},{TAG_DONE,0}};
    driver=OOP_NewObject(NULL,(CONST_STRPTR)CLID_Hidd_PCIECAM,tags);
    if(!driver) return 88;
    read_base=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDriver,0);
    IPTR okay=Supervisor((APTR)check_driver);
    OOP_DisposeObject(driver);driver=NULL;
    if(!okay) return 89;
    mark("[VM] PCI HIDD registration begin\n");
    if(!register_bridge(tags)) return 90;
    OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIECAM);
    mark("[VM] PCI subsystem enumerate + remove PASS\n");
    mark("[VM] virtio common config via MapPCI PASS\n");
    mark("[VM] virtio reset acknowledged PASS\n");
    /* The permanent module supplies its own callback and mappings. */
    struct Library *resident=OpenLibrary((CONST_STRPTR)"DEVS:Drivers/vmpci.hidd",0);
    if(!resident) return 92;
    CloseLibrary(resident); /* noexpunge, retained registration */
    mark("[VM] PCI resident install PASS\n");
    mark("[VM] PCI HIDD class + reserved console PASS\n");
    return 0;
}
