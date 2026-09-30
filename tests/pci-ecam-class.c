// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define PROTO_UTILITY_H
#define PROTO_OOP_H
#include <oop/oop.h>
static IPTR super_result;
static unsigned completions;
static void complete_write(void) { ++completions; }
static IPTR test_super(OOP_Class *cl,OOP_Object *o,OOP_Msg msg)
{ (void)cl;(void)o;(void)msg;return super_result; }
/* Only host substitutes: utility lookup for the flat fixture tags and the
 * parent object allocator. Driver methods below are compiled unchanged. */
static IPTR GetTagData(Tag wanted,IPTR fallback,const struct TagItem *tags)
{
    for (;tags && tags->ti_Tag; ++tags) if(tags->ti_Tag==wanted) return tags->ti_Data;
    return fallback;
}
#include "pciecam_class.c"
int main(void)
{
    void *memory;
    assert(!posix_memalign(&memory,1u<<20,1u<<20));
    memset(memory,0xa5,1u<<20);
    struct PCIECAMBase base={0}; base.config_attr=0x10000; base.driver_attr=0x20000;
    OOP_Class cl={0}; cl.UserData=&base; cl.cl_DoSuperMethod=test_super;
    struct PCIECAMData data={0}; OOP_Object *object=(void *)&data;
    struct PCIECAMConfig host={0};
    host.config=(struct AROSPCIECAM){memory,1u<<20,0,0};
    host.complete_config_write=complete_write;
    host.window_count=1;
    host.windows[0]=(struct PCIECAMWindow){0x10000000,0x20000000,0x2000};
    host.reserved_root_slots=1u<<2;
    struct TagItem tags[]={{base.config_attr,(IPTR)&host},{TAG_DONE,0}};
    struct pRoot_New newmsg={0,tags};
    super_result=(IPTR)object;
    assert(PCIECAM__Root__New(&cl,NULL,&newmsg)==object);
    struct pHidd_PCIDriver_ReadConfigLong read={0,NULL,0,2,0,0};
    assert(PCIECAM__Hidd_PCIDriver__ReadConfigLong(&cl,object,&read)==0xffffffff);
    struct pHidd_PCIDriver_WriteConfigWord write={0,NULL,0,2,0,4,0x1234};
    PCIECAM__Hidd_PCIDriver__WriteConfigWord(&cl,object,&write);
    assert(((uint16_t *)memory)[((2u<<15)+4)/2]==0xa5a5);
    assert(completions==0);
    write.dev=3; PCIECAM__Hidd_PCIDriver__WriteConfigWord(&cl,object,&write);
    assert(((uint16_t *)memory)[((3u<<15)+4)/2]==0x1234);
    assert(completions==1);
    assert(((uint16_t *)memory)[((3u<<15)+6)/2]==0xa5a5);
    struct pHidd_PCIDriver_MapPCI map={0,(void *)0x10001000,0x1000};
    assert(PCIECAM__Hidd_PCIDriver__MapPCI(&cl,object,&map)==(void *)0x20001000);
    map.Length++; assert(PCIECAM__Hidd_PCIDriver__MapPCI(&cl,object,&map)==(void *)-1);
    map.Length=0; assert(PCIECAM__Hidd_PCIDriver__MapPCI(&cl,object,&map)==(void *)-1);
    assert(!PCIECAM__Hidd_PCIDriver__AllocPCIMem(&cl,object,NULL));
    assert(!PCIECAM__Hidd_PCIDriver__AddInterrupt(&cl,object,NULL));
    assert(PCIECAM__Hidd_PCIDriver__CPUtoPCI(&cl,object,NULL)==(void *)-1);
    host.window_count=2;host.windows[1]=host.windows[0];
    assert(!PCIECAM__Root__New(&cl,NULL,&newmsg));
    host.window_count=1;host.windows[0].cpu=UINT64_MAX-4095;
    assert(!PCIECAM__Root__New(&cl,NULL,&newmsg));
    host.windows[0].cpu=0x20000000;super_result=0;
    assert(!PCIECAM__Root__New(&cl,NULL,&newmsg));
    free(memory);
    puts("pci-ecam-class result=PASS real-methods,reserved-slots,translation,bounds,no-implicit-DMA-IRQ");
}
