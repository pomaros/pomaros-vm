// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/openfirmware.h>
#include "ecam/pciecam.h"
#undef OOPBase
#undef UtilityBase
struct Library *OOPBase;
int __nowbsupport=1;
int __nostdiowin=1;
struct WBStartup *WBenchMsg;
APTR OpenFirmwareBase;
static OOP_Object *driver;
static struct PCIECAMConfig host;
static OOP_MethodID read_base;
static void mark(const char *s) { while(*s) RawPutChar(*s++); }
static uint32_t cell(const UBYTE *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void complete_write(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static IPTR reserve_console(void)
{
    for(unsigned slot=0;slot<32;++slot)
        if(AROSPCIECAMRead(&host.config,0,slot,0,0,4)==0x10431af4)
            host.reserved_root_slots |= UINT32_C(1)<<slot;
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
int main(void)
{
    mark("[VM] PCI HIDD load begin\n");
    OOPBase=OpenLibrary((CONST_STRPTR)"SYS:Libs/oop.library",0);
    if(!OOPBase) { mark("[VM] PCI HIDD fail oop.library\n"); return 81; }
    mark("[VM] PCI HIDD oop loaded\n");
    if(!OpenLibrary((CONST_STRPTR)"SYS:Devs/Drivers/hiddclass.hidd",0)) { mark("[VM] PCI HIDD fail hiddclass\n"); return 82; }
    mark("[VM] PCI HIDD hiddclass loaded\n");
    if(!OpenLibrary((CONST_STRPTR)"SYS:Devs/Drivers/pci.hidd",0)) { mark("[VM] PCI HIDD fail pci\n"); return 83; }
    if(!OpenLibrary((CONST_STRPTR)"SYS:Devs/Drivers/pciecam.hidd",0)) { mark("[VM] PCI HIDD fail pciecam\n"); return 84; }
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
    OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIECAM);
    if(!okay) return 89;
    /* Keep libraries resident, as a driver startup would. No instance retains
     * this command's completion callback after disposal. */
    mark("[VM] PCI HIDD class + reserved console PASS\n");
    return 0;
}
