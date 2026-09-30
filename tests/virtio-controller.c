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
#include <proto/exec.h>
#include <proto/kernel.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <hidd/hidd.h>
#include <hidd/pci.h>
#include <hidd/virtio.h>
#include <hidd/storage.h>
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "virtio_queue.h"
#include "virtio_debug.h"
static unsigned fail, stop_fail, maps, unmaps, queues, frees, stops, callbacks, published, restored;
static struct virtio_Controller controller;
static OOP_Class klass;
static IPTR parent(OOP_Class *cl,OOP_Object *o,OOP_Msg msg)
{ (void)cl;(void)o;return *msg==1 ? (IPTR)&controller : 0; }
static IPTR tagdata(Tag wanted,IPTR fallback,const struct TagItem *tags)
{ for(;tags && tags->ti_Tag;++tags) if(tags->ti_Tag==wanted) return tags->ti_Data;return fallback; }
void VirtIO__Root__Dispose(OOP_Class *,OOP_Object *,OOP_Msg);
static void dispose(OOP_Object *o) { OOP_MethodID m=2;VirtIO__Root__Dispose(&klass,o,&m); }
#undef GetTagData
#define GetTagData tagdata
#undef OOP_DisposeObject
#define OOP_DisposeObject dispose
#undef OOP_SetAttrs
#define OOP_SetAttrs(o,t) ((void)(o),(void)(t))
#undef HIDD_PCIDevice_ReadConfigWord
#define HIDD_PCIDevice_ReadConfigWord(o,r) ((void)(o),(void)(r),7)
#undef HIDD_PCIDevice_WriteConfigWord
#define HIDD_PCIDevice_WriteConfigWord(o,r,v) ((void)VirtIOBase,(void)(o),(void)(r),assert((v)==7),++restored)
#undef HIDD_StorageController_AddBus
#define HIDD_StorageController_AddBus(o,c,t) ((void)(o),(void)(c),(void)(t),fail==6 ? NULL : (OOP_Object *)1)
#undef AddResetCallback
#define AddResetCallback(i) ((void)(i),++callbacks)
#undef AddTail
#define AddTail(l,n) ((void)(l),(void)(n),++published)
#undef bug
#define bug(...) ((void)0)
#define AROS_INTH1(name,type,arg) BOOL name(type arg)
#define AROS_INTFUNC_INIT
#define AROS_INTFUNC_EXIT
#define LC_LIBDEFS_FILE "/dev/null"
#define GM_UNIQUENAME(n) n
const char LibName[]="virtio.device";
#include "virtio_controllerclass.c"
BOOL virtio_hw_map(device_t d) { if(fail==1)return FALSE;++maps;d->dev_CommonCfg=(void *)1;return TRUE; }
void virtio_hw_unmap(device_t d) { if(d->dev_CommonCfg){assert(stops && !stop_fail);++unmaps;}d->dev_CommonCfg=NULL; }
BOOL virtio_hw_reset(device_t d) { (void)d;return fail!=2; }
BOOL virtio_hw_stop(device_t d) { (void)d;++stops;return !stop_fail; }
BOOL virtio_hw_negotiate_features(device_t d,UQUAD f) { (void)d;(void)f;return fail!=3; }
void virtio_hw_read_blk_config(device_t d) { (void)d; }
struct virtio_queue *virtio_alloc_queue(device_t d,int q,int size)
{ (void)d;(void)q;(void)size;if(fail==4)return NULL;++queues;return calloc(1,sizeof(struct virtio_queue)); }
void virtio_free_queue(struct virtio_queue *q) { assert(stops && !stop_fail);++frees;free(q); }
BOOL virtio_hw_setup_queue(device_t d,UWORD n,struct virtio_queue *q)
{ (void)d;(void)n;(void)q;return fail!=5; }
BOOL Hidd_VirtIOBus_Start(OOP_Object *o,struct VirtIOBase *b) { (void)o;(void)b;return TRUE; }
int main(void)
{
    struct VirtIOBase base={0};klass.UserData=&base;klass.cl_DoSuperMethod=parent;
    for(unsigned which=1;which<=7;++which) {
        for(unsigned stuck=0;stuck<=1;++stuck) {
            __typeof__(*(device_t)0) dev={0};dev.dev_VirtIOBase=&base;
            memset(&controller,0,sizeof(controller));
            fail=which;stop_fail=stuck;maps=unmaps=queues=frees=stops=callbacks=published=restored=0;
            struct VirtIOBase *VirtIOBase=&base;
            struct TagItem tags[]={{aHidd_DriverData,(IPTR)&dev},{TAG_DONE,0}};
            struct pRoot_New msg={1,tags};
            OOP_Object *o=VirtIO__Root__New(&klass,NULL,&msg);
            if(which==7) {
                assert(o && callbacks==1 && published==1 && !stops && !frees);
                dispose(o);assert(!frees && !unmaps); /* Published bus retained. */
                free(dev.dev_VQ); /* fixture-only teardown */
            } else {
                assert(!o && !dev.dev_Controller && !callbacks && !published);
                assert(stops==(which!=1));
                if(stuck && which!=1) {
                    assert(dev.dev_Quarantined && !frees && !unmaps && !restored);
                    free(dev.dev_VQ); /* simulated device gone */
                } else {
                    assert(!dev.dev_Quarantined && queues==frees && maps==unmaps);
                    assert(restored==(which!=1));
                }
            }
        }
    }
    puts("virtio-controller result=PASS real-constructor,failure-unwind,reset-quarantine,published-retention");
}
