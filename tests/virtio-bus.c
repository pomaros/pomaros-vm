// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define _AROS_TYPES_TIMEVAL_S_H_
#define __NOLIBBASE__
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <aros/debug.h>
#include <aros/atomic.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <proto/oop.h>
#include <proto/utility.h>
#include <proto/expansion.h>
#include <libraries/expansion.h>
#include <dos/dosextens.h>
#include <hidd/pci.h>
#include <hidd/storage.h>
#include <hidd/virtio.h>
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "virtio_queue.h"
static unsigned fail, libraries, irqs, workers, units, ids;
static struct virtio_Unit unit;
static struct Node idnode;
static OOP_Class busclass, unitclass;
static IPTR parent(OOP_Class *cl,OOP_Object *o,OOP_Msg msg)
{ (void)cl;(void)o;(void)msg;return 0; }
static void *open_library(void) { if(fail==1)return NULL;++libraries;return (void *)1; }
static void *new_unit(void) { if(fail==2)return NULL;++units;return &unit; }
static void *new_id(void) { if(fail==3)return NULL;++ids;return &idnode; }
static int add_irq(void) { if(fail==4)return 0;++irqs;return 1; }
static void *new_worker(void) { if(fail==6)return NULL;++workers;return (void *)1; }
#undef TaggedOpenLibrary
#define TaggedOpenLibrary(x) ((void)(x),open_library())
#undef CloseLibrary
#define CloseLibrary(x) ((void)(x),--libraries)
#undef OOP_NewObject
#define OOP_NewObject(c,n,t) ((void)(c),(void)(n),(void)(t),(OOP_Object *)new_unit())
#undef OOP_DisposeObject
#define OOP_DisposeObject(o) ((void)(o),--units)
#undef HIDD_Storage_AllocateID
#define HIDD_Storage_AllocateID(o,t) ((void)(o),(void)(t),new_id())
#undef HIDD_Storage_ReleaseID
#define HIDD_Storage_ReleaseID(o,i) ((void)(o),(void)(i),--ids)
#undef HIDD_PCIDriver_AddInterrupt
#define HIDD_PCIDriver_AddInterrupt(o,d,i) ((void)(o),(void)(d),(void)(i),add_irq())
#undef HIDD_PCIDriver_RemoveInterrupt
#define HIDD_PCIDriver_RemoveInterrupt(o,d,i) ((void)(o),(void)(d),(void)(i),--irqs)
#undef NewCreateTask
#define NewCreateTask(...) new_worker()
#undef MakeDosNode
#define MakeDosNode(p) ((void)(p),(struct DeviceNode *)NULL)
#undef AddBootNode
#define AddBootNode(p,f,d,c) ((void)(p),(void)(f),(void)(d),(void)(c),1)
#undef FreeVec
#define FreeVec(p) free(p)
#undef FreeMem
#define FreeMem(p,n) ((void)(n),free(p))
#undef AllocMem
#define AllocMem(n,f) ((void)(f),calloc(1,n))
#undef AllocVec
#define AllocVec(n,f) ((void)(f),calloc(1,n))
#undef CopyMem
#define CopyMem(s,d,n) memcpy(d,s,n)
#undef GetTagData
#define GetTagData(t,d,l) ((void)(t),(void)(l),(IPTR)(d))
#undef AROS_ATOMIC_INC
#define AROS_ATOMIC_INC(v) (++(v))
#undef Signal
#define Signal(t,m) ((void)(t),(void)(m))
#undef Wait
#define Wait(m) ((void)(m))
#undef bug
#define bug(...) ((void)0)
#define AROS_INTH1(name,type,arg) BOOL name(type arg)
#define AROS_INTFUNC_INIT
#define AROS_INTFUNC_EXIT
#define LC_LIBDEFS_FILE "/dev/null"
#define GM_UNIQUENAME(n) n
#define MOD_NAME_STRING "virtio.device"
const char LibName[]="virtio.device";
#include "virtio_busclass.c"
BOOL virtio_hw_route_irq(device_t d) { (void)d;return fail!=8; }
BOOL virtio_hw_driver_ok(device_t d) { (void)d;return fail!=5; }
ULONG virtio_process_used(struct virtio_queue *q) { (void)q;return 0; }
int main(void)
{
    struct VirtIOBase base={0};base.busClass=&busclass;base.unitClass=&unitclass;
    busclass.UserData=&base;busclass.cl_DoSuperMethod=parent;
    for(fail=1;fail<=8;++fail) {
        __typeof__(*(device_t)0) dev={0};struct virtio_queue queue={0};
        dev.dev_VQ=&queue;dev.dev_Capacity=1024;dev.dev_BlkSize=512;
        struct virtio_Bus bus={0};bus.ab_Base=&base;bus.ab_Dev=&dev;
        bus.ab_UnitMax=1;bus.ab_Units=calloc(1,sizeof(OOP_Object *));
        memset(&unit,0,sizeof(unit));libraries=irqs=workers=units=ids=0;
        BOOL ok=Hidd_VirtIOBus_Start((OOP_Object *)&bus,&base);
        if(fail<=6 || fail==8) {
            assert(!ok && !libraries && !irqs && !workers && !bus.ab_UnitCnt);
            OOP_MethodID m=0;VirtIOBus__Root__Dispose(&busclass,(OOP_Object *)&bus,&m);
            assert(!units && !ids);
        } else {
            assert(ok && !libraries && irqs==1 && workers==1 && units==1 && ids==1);
            assert(bus.ab_UnitCnt==1);
            free(bus.ab_Units); /* Host fixture, no real worker/IRQ exists. */
        }
    }
    puts("virtio-bus result=PASS real-startup,allocation-IRQ-worker-failures,no-orphan-worker");
}
