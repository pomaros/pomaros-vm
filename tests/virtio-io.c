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
#include <aros/atomic.h>
#include <aros/symbolsets.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <hidd/pci.h>
#include "virtio_intern.h"
#include "virtio_queue.h"
static struct Node *linked;
static unsigned removes, replies, submissions;
static int reject, immediate_free;
static struct virtio_request captured;
static void add(struct Node *n) { assert(!linked);linked=n; }
static void remove_node(struct Node *n) { assert(linked==n);linked=NULL;++removes; }
#undef AddHead
#define AddHead(l,n) ((void)(l),add(n))
#undef Remove
#define Remove(n) remove_node(n)
#undef ObtainSemaphore
#define ObtainSemaphore(s) ((void)(s))
#undef ReleaseSemaphore
#define ReleaseSemaphore(s) ((void)(s))
#undef ReplyMsg
#define ReplyMsg(m) ((void)(m),++replies)
#undef SetMem
#define SetMem(p,v,n) memset(p,v,n)
#undef AROS_LH1
#define AROS_LH1(ret,name,arg,basetype,base,num,lib) ret test_##name(arg)
#undef AROS_LHA
#define AROS_LHA(type,name,reg) type name
#undef AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_INIT
#undef AROS_LIBFUNC_EXIT
#define AROS_LIBFUNC_EXIT
#define LC_LIBDEFS_FILE "/dev/null"
#include "virtio_io.c"
int virtio_submit_blk_request(struct virtio_queue *q,struct virtio_request *r)
{
    (void)q;++submissions;captured=*r;
    assert(!linked);assert(!(r->vr_IORequest->io_Flags&IOF_QUICK));
    if(reject) return -1;
    if(immediate_free) free(r->vr_IORequest);
    return 0;
}
static struct virtio_Unit unit;
static unsigned char buffer[8192];
static struct IOExtTD *request(UWORD command,UBYTE flags,UQUAD offset,ULONG length)
{
    struct IOExtTD *r=calloc(1,sizeof(*r));assert(r);
    r->iotd_Req.io_Unit=(void *)&unit;r->iotd_Req.io_Command=command;
    r->iotd_Req.io_Flags=flags;r->iotd_Req.io_Data=buffer;
    r->iotd_Req.io_Offset=(ULONG)offset;r->iotd_Req.io_Actual=offset>>32;
    r->iotd_Req.io_Length=length;return r;
}
static void bad(UQUAD offset,ULONG length,BOOL null_data,BYTE error)
{
    struct IOExtTD *r=request(NSCMD_TD_WRITE64,0,offset,length);
    if(null_data)r->iotd_Req.io_Data=NULL;
    unsigned before=submissions,reply=replies,removed=removes;
    test_BeginIO((void *)r);
    assert(r->iotd_Req.io_Error==error && !r->iotd_Req.io_Actual);
    assert(submissions==before && replies==reply+1 && removes==removed+1 && !linked);free(r);
}
int main(void)
{
    __typeof__(*(device_t)0) dev={0};struct virtio_Bus bus={0};
    bus.ab_Dev=&dev;unit.au_Bus=&bus;unit.au_SecShift=9;unit.au_SecCnt=16;
    dev.dev_VQ=(void *)1;dev.dev_Features=(UQUAD)1<<VIRTIO_BLK_F_FLUSH;
    bad(1,512,0,IOERR_BADADDRESS);bad(0,0,0,IOERR_BADLENGTH);
    bad(0,513,0,IOERR_BADLENGTH);bad(0,512,1,IOERR_BADADDRESS);
    bad(16*512,512,0,IOERR_BADADDRESS);bad(15*512,1024,0,IOERR_BADADDRESS);
    bad(UINT64_MAX-511,512,0,IOERR_BADADDRESS);
    unit.au_SecShift=12;unit.au_SecCnt=2;
    bad(512,4096,0,IOERR_BADADDRESS);bad(0,512,0,IOERR_BADLENGTH);
    unit.au_SecShift=9;unit.au_SecCnt=16;
    for(unsigned quick=0;quick<2;++quick) for(unsigned flush=0;flush<2;++flush) {
        reject=1;struct IOExtTD *r=request(flush?CMD_UPDATE:CMD_WRITE,quick?IOF_QUICK:0,0,512);
        unsigned reply=replies,removed=removes;test_BeginIO((void *)r);
        assert(r->iotd_Req.io_Error==IOERR_ABORTED && !r->iotd_Req.io_Actual);
        assert(replies==reply+!quick && removes==removed+1 && !linked);
        assert(!!(r->iotd_Req.io_Flags&IOF_QUICK)==quick);free(r);
    }
    reject=0;immediate_free=1;
    /* The completion recipient can free the request before submission returns. */
    test_BeginIO((void *)request(CMD_READ,IOF_QUICK,15*512,512));
    assert(captured.vr_Header.sector==15 && captured.vr_DataLength==512);
    test_BeginIO((void *)request(CMD_UPDATE,IOF_QUICK,0,0));
    assert(captured.vr_Header.type==VIRTIO_BLK_T_FLUSH);
    unit.au_SecShift=12;unit.au_SecCnt=2;
    test_BeginIO((void *)request(CMD_WRITE,IOF_QUICK,4096,4096));
    assert(captured.vr_Header.sector==8 && captured.vr_DataLength==4096);
    unit.au_SecShift=9;unit.au_SecCnt=UINT64_MAX>>9;
    test_BeginIO((void *)request(NSCMD_TD_WRITE64,IOF_QUICK,((UQUAD)1<<32)+512,512));
    assert(captured.vr_Header.sector==(((UQUAD)1<<32)+512)/512);
    puts("virtio-io result=PASS real-BeginIO,bounds,submit-rejection,single-remove,immediate-free,64-bit-offset");
}
