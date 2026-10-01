// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __NOLIBBASE__
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/exec.h>
#include <proto/kernel.h>
#include <proto/oop.h>
#include <proto/openfirmware.h>
#include <aros/kernel.h>
#include <exec/interrupts.h>
#include "vmpci.h"
static uint8_t config[256];
static uint32_t table_words[8];
static unsigned live,handlers,alloc_fail,config_fail,handler_fail,enable_fail,last_source;
static IPTR owner=0x2222,bar_bytes=sizeof(table_words);
static void *allocate(size_t n) { if(alloc_fail)return NULL;++live;return calloc(1,n); }
static void release(void *p) { assert(live);--live;free(p); }
static IPTR method(OOP_Object *o,OOP_Msg msg)
{
    (void)o;OOP_MethodID id=*msg;
    unsigned reg=((struct pHidd_PCIDevice_ReadConfigByte *)msg)->reg;
    assert(reg<256);
    if(id==moHidd_PCIDevice_WriteConfigWord) {
        unsigned v=((struct pHidd_PCIDevice_WriteConfigWord *)msg)->val;
        if(enable_fail && reg==0x42) v&=~0x8000;
        config[reg]=v;config[reg+1]=v>>8;return 0;
    }
    unsigned width=id==moHidd_PCIDevice_ReadConfigByte?1:id==moHidd_PCIDevice_ReadConfigWord?2:4;
    uint32_t v=0;for(unsigned i=0;i<width;++i)v|=(uint32_t)config[reg+i]<<(8*i);
    return v;
}
static void attribute(OOP_Object *o,OOP_AttrID id,IPTR *out)
{
    (void)o;
    if(id==aoHidd_PCIDevice_Driver)*out=owner;
    else if(id==aoHidd_PCIDevice_Base0)*out=0x10000000;
    else if(id==aoHidd_PCIDevice_Size0)*out=bar_bytes;
    else *out=0;
}
static unsigned configure(unsigned source,struct TagItem *tags)
{ assert(tags[0].ti_Data==2 && tags[1].ti_Data==1);last_source=source;return !config_fail; }
static void *install(unsigned source,void (*code)(void *,void *),void *data,void *unused)
{ assert(source==last_source && code && data && !unused);if(handler_fail)return NULL;++handlers;return (void *)0x1234; }
static void uninstall(void *cookie)
{ assert(cookie==(void *)0x1234 && handlers && !(config[0x43]&0x80));--handlers; }
#undef OOP_DoMethod
#define OOP_DoMethod(o,m) ((void)base,method(o,m))
#undef OOP_GetAttr
#define OOP_GetAttr(o,a,v) ((void)base,attribute(o,a,v))
#undef OOP_ObtainAttrBase
#define OOP_ObtainAttrBase(x) ((void)(x),1)
#undef OOP_GetMethodID
#define OOP_GetMethodID(x,i) ((void)(x),(i))
#undef OF_FindProperty
#define OF_FindProperty(n,p) ((void)OpenFirmwareBase,(void)(n),(void)(p),(void *)NULL)
#undef OF_FindNodeByCompatible
#define OF_FindNodeByCompatible(n,c) ((void)(n),(void)(c),(void *)NULL)
#undef OF_GetPropLen
#define OF_GetPropLen(p) ((void)(p),0u)
#undef OF_GetPropValue
#define OF_GetPropValue(p) (p)
#undef AllocMem
#define AllocMem(n,f) ((void)(f),allocate(n))
#undef FreeMem
#define FreeMem(p,n) ((void)(n),release(p))
#undef KrnModifyIRQA
#define KrnModifyIRQA(s,t) ((void)KernelBase,configure(s,t))
#undef KrnAddIRQHandler
#define KrnAddIRQHandler(s,c,d,u) install(s,c,d,u)
#undef KrnRemIRQHandler
#define KrnRemIRQHandler(c) ((void)KernelBase,uninstall(c))
#undef AddTail
#define AddTail(l,n) ADDTAIL(l,n)
#undef Remove
#define Remove(n) REMOVE(n)
#undef ObtainSemaphore
#define ObtainSemaphore(s) ((void)(s))
#undef ReleaseSemaphore
#define ReleaseSemaphore(s) ((void)(s))
#undef InitSemaphore
#define InitSemaphore(s) ((void)(s))
#undef Disable
#define Disable() ((void)0)
#undef Enable
#define Enable() ((void)0)
#undef Supervisor
#define Supervisor(fn) ((IPTR (*)(void))(fn))()
#define AROS_INTC1(n,d) ((void)(n),(void)(d))
#include "vmpci_irq.c"
static void handler_code(void) {}
int main(void)
{
    struct VMPciBase base={0};assert(VMPciIRQInit(&base,NULL,NULL));assert(!base.host.irq.add);
    base.bridge=(void *)owner;base.msi_frame=0x1fff0000;base.msi_first=128;base.msi_count=128;
    base.host.window_count=1;
    base.host.windows[0]=(struct PCIECAMWindow){0x10000000,(uintptr_t)table_words,sizeof(table_words)};
    struct Interrupt handler={0};handler.is_Code=(VOID_FUNC)handler_code;
    OOP_Object *device=(void *)0x1111;
    config[4]=2;config[0x34]=0x40;config[0x40]=0x11;config[0x42]=1;table_words[7]=1;
    alloc_fail=1;assert(!add_irq(&base,device,&handler) && !live && !handlers);alloc_fail=0;
    owner=0;assert(!add_irq(&base,device,&handler));owner=0x2222;
    config[0x43]=0x80;assert(!add_irq(&base,device,&handler));config[0x43]=0;
    config[0x41]=0x40;assert(!add_irq(&base,device,&handler));config[0x41]=0;
    config[4]=0;assert(!add_irq(&base,device,&handler));config[4]=2;
    table_words[7]=0;assert(!add_irq(&base,device,&handler));table_words[7]=1;
    bar_bytes=16;assert(!add_irq(&base,device,&handler));bar_bytes=32;
    config[0x44]=6;assert(!add_irq(&base,device,&handler));config[0x44]=0;
    config[0x44]=8;assert(!add_irq(&base,device,&handler));config[0x44]=0;
    base.host.window_count=0;assert(!add_irq(&base,device,&handler));base.host.window_count=1;
    config_fail=1;assert(!add_irq(&base,device,&handler) && live==1 && !handlers);config_fail=0;
    handler_fail=1;assert(!add_irq(&base,device,&handler) && live==1 && !handlers);handler_fail=0;
    enable_fail=1;assert(!add_irq(&base,device,&handler) && live==1 && !handlers);enable_fail=0;
    assert(config[4]==2 && config[5]==0 && config[0x42]==1 && config[0x43]==0);
    for(unsigned i=0;i<4;++i)assert(!table_words[i]);
    assert(add_irq(&base,device,&handler) && live==1 && handlers==1);
    unsigned first=last_source;
    assert(table_words[0]==0x1fff0040 && !table_words[1] && table_words[2]==first && !table_words[3]);
    assert(config[0x43]==0x80 && config[5]==4);
    assert(!add_irq(&base,device,&handler) && live==1 && handlers==1);
    remove_irq(&base,(void *)0x3333,&handler);assert(handlers==1);
    remove_irq(&base,device,&handler);assert(live==1 && !handlers);
    assert(!config[0x43] && !config[5]);for(unsigned i=0;i<4;++i)assert(!table_words[i]);
    remove_irq(&base,device,&handler);assert(live==1 && !handlers);
    /* More sessions than the entire controller capacity must remain usable. */
    for(unsigned session=0;session<512;++session) {
        assert(add_irq(&base,device,&handler));assert(last_source==first);
        remove_irq(&base,device,&handler);
        assert(live==1 && !handlers && base.msi_next==1);
    }
    OOP_Object *other=(void *)0x3333;
    assert(add_irq(&base,other,&handler));assert(last_source==first+1);
    remove_irq(&base,other,&handler);assert(live==2 && !handlers);
    base.msi_next=base.msi_count;
    assert(add_irq(&base,device,&handler));assert(last_source==first);
    remove_irq(&base,device,&handler);
    assert(!add_irq(&base,(void *)0x4444,&handler));
    assert(live==2 && !handlers);
    /* The resident bridge owns reservations until reboot. Dispose the mock
     * bridge explicitly so the sanitizer also detects accidental extra owners. */
    while(!IsListEmpty(&base.irq_routes)) {
        struct IRQRoute *route=(void *)base.irq_routes.lh_Head;
        assert(!route->handler && !route->cookie && !route->table);
        REMOVE(&route->node);release(route);
    }
    assert(!live);
    puts("vmpci-irq result=PASS real-routing,rollback,512-reopens,per-device-source-isolation");
}
