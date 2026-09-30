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
#undef OOPBase
#undef UtilityBase
struct Library *OOPBase;
int __nowbsupport=1;
int __nostdiowin=1;
struct WBStartup *WBenchMsg;
APTR OpenFirmwareBase;
APTR KernelBase;
static OOP_AttrBase device_attr;
static unsigned enumerated,console_seen,expected;
static OOP_Object *driver;
static struct PCIECAMConfig host;
static OOP_MethodID read_base;
static void mark(const char *s) { while(*s) RawPutChar(*s++); }
static uint32_t cell(const UBYTE *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void complete_write(void) { __asm__ volatile("dsb sy" ::: "memory"); }
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
    if(vendor==0x1af4) { ++enumerated; if(product==0x1043) ++console_seen; }
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
    OOP_DoMethod(pci,(OOP_Msg)&enumerate);
    unsigned before=enumerated;
    struct pHW_RemoveDriver remove={OOP_GetMethodID((CONST_STRPTR)IID_HW,moHW_RemoveDriver),registered};
    if(!OOP_DoMethod(pci,(OOP_Msg)&remove)) return 0;
    enumerated=0;
    OOP_DoMethod(pci,(OOP_Msg)&enumerate);
    OOP_DisposeObject(pci);OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    KrnSetProtection((void *)host.config.mapping,host.config.bytes,MAP_Readable|MAP_Writable|MAP_Supervisor);
    return expected && before==expected && !enumerated && !console_seen;
}
int main(void)
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
    host.complete_config_write=complete_write;
    Supervisor((APTR)reserve_console);
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
    /* Keep libraries resident, as a driver startup would. No instance retains
     * this command's completion callback after disposal. */
    mark("[VM] PCI HIDD class + reserved console PASS\n");
    return 0;
}
