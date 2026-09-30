// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#define main abi_main
#include "abi-corpus/portable-probe.c"
#undef main
#include <proto/openfirmware.h>
APTR OpenFirmwareBase;

static uint32_t read_cell(const UBYTE *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint64_t probe_ecam;
static IPTR read_endpoints(void)
{
    unsigned endpoints=0;
    for(unsigned dev=0;dev<32;++dev) {
        uint32_t id=*(volatile uint32_t *)(uintptr_t)(probe_ecam+((uint64_t)dev<<15));
        if((id&0xffff)==0x1af4) ++endpoints;
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
    probe_ecam=ecam;
    unsigned endpoints=(unsigned)Supervisor((APTR)read_endpoints);
    if(!endpoints) return 77;
    const char *message="[VM] OpenFirmware FDT + PCI config PASS\n";
    while(*message) RawPutChar(*message++);
    return 0;
}
int main(int argc,char **argv)
{
    int result=abi_main(argc,argv);
    if(result!=73) return result;
    result=platform_probe();
    return result?result:73;
}
