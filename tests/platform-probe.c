// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#define main abi_main
#include "abi-corpus/portable-probe.c"
#undef main
#include <proto/openfirmware.h>
#include "ecam/access.h"
APTR OpenFirmwareBase;

static uint32_t read_cell(const UBYTE *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static struct AROSPCIECAM probe_ecam;
static IPTR read_endpoints(void)
{
    unsigned endpoints=0;
    for(unsigned dev=0;dev<32;++dev) {
        uint32_t id=AROSPCIECAMRead(&probe_ecam,probe_ecam.first_bus,dev,0,0,4);
        if((id&0xffff)==0x1af4) {
            unsigned bus=probe_ecam.first_bus;
            if(AROSPCIECAMRead(&probe_ecam,bus,dev,0,0,2)!=(id&0xffff) ||
               AROSPCIECAMRead(&probe_ecam,bus,dev,0,0,1)!=(id&0xff)) return 0;
            uint32_t command=AROSPCIECAMRead(&probe_ecam,bus,dev,0,4,2);
            if(!AROSPCIECAMWrite(&probe_ecam,bus,dev,0,4,2,command) ||
               AROSPCIECAMRead(&probe_ecam,bus,dev,0,4,2)!=command) return 0;
            ++endpoints;
        }
    }
    return endpoints;
}
static int platform_probe(void)
{
    OpenFirmwareBase=OpenResource((CONST_STRPTR)"openfirmware.resource");
    if(!OpenFirmwareBase || !OF_OpenKey("/") || OF_OpenKey("/this-node-does-not-exist")) return 75;
    void *pci=OF_FindNodeByCompatible(NULL,"pci-host-ecam-generic");
    void *reg=pci?OF_FindProperty(pci,"reg"):NULL;
    if(!reg || OF_GetPropLen(reg)!=16) return 76;
    const UBYTE *cells=OF_GetPropValue(reg);
    uint64_t ecam=((uint64_t)read_cell(cells)<<32)|read_cell(cells+4);
    void *buses=OF_FindProperty(pci,"bus-range");
    if(!buses || OF_GetPropLen(buses)!=8) return 76;
    const UBYTE *bus_cells=OF_GetPropValue(buses);
    probe_ecam.mapping=(void *)(uintptr_t)ecam;
    probe_ecam.bytes=((uint64_t)read_cell(cells+8)<<32)|read_cell(cells+12);
    probe_ecam.first_bus=read_cell(bus_cells);
    probe_ecam.last_bus=read_cell(bus_cells+4);
    unsigned endpoints=(unsigned)Supervisor((APTR)read_endpoints);
    if(!endpoints) return 77;
    const char *message="[VM] OpenFirmware FDT + PCI config PASS\n";
    while(*message) RawPutChar(*message++);
    return 0;
}
int test_command_startup(void);
int main(int argc,char **argv)
{
    int result=abi_main(argc,argv);
    if(result!=73) return result;
    result=platform_probe();
    if(result) return result;
    if(!test_command_startup()) return 78;
    return result?result:73;
}
