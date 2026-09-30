// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define __NOLIBBASE__
#define _AROS_TYPES_TIMEVAL_S_H_
#include <proto/exec.h>
#include <aros/macros.h>
#include <hardware/usb/xhci.h>
#include <devices/usbhardware.h>
#define UHWCMD_H
#define XHCIPROTO_H
#define XHCICHIP_H
#define XHCI_EVENT_RING_TRBS 8
#define MAX_DEVENDPOINTS 32
#define HCF_CTX64 1
#define RINGENDCFLAG (1U << 15)
struct PCIController { void *hc_PCIDriverObject,*hc_Device; ULONG hc_Flags; };
struct pcisusbXHCIRing {
    struct xhci_trb ring[XHCI_EVENT_RING_TRBS];
    struct IORequest **ringio;
    volatile UWORD next,end;
};
struct xhciCancelPlan { UWORD first,last,next;BOOL moveDequeue,cycle; };
struct xhciControlTRBs { UWORD setup,dataFirst,dataLast,status; };
struct allocation { APTR dmaa_Ptr,dmaa_DMA; };
struct pciusbXHCIDevice { struct allocation dc_EPAllocs[MAX_DEVENDPOINTS],dc_SlotCtx; UBYTE dc_SlotID; };
struct pciusbXHCIIODevPrivate {
    struct pciusbXHCIDevice *dpDevice;ULONG dpEPID;
    UWORD dpSTRB,dpSttTRB,dpTxSTRB,dpTxETRB;
};
static unsigned depth,writes,invalidates,commands;
static void xhciRingLock(void) { ++depth; }
static void xhciRingUnlock(void) { assert(depth);--depth; }
static void cache(APTR p,ULONG bytes,ULONG flags)
{
    assert(p);
    if(flags==CACRF_InvalidateD) { assert(!depth && (bytes==32 || bytes==64));++invalidates; }
    else { assert(depth && bytes==sizeof(struct xhci_trb));++writes; }
}
#define CPUTOPCI(h,o,p) ((void)(h),(void)(o),(p))
#define xhciSetPointer(h,d,p) do { (void)(h);(d).addr64=AROS_QUAD2LE(p); } while(0)
#undef CacheClearE
#define CacheClearE(p,n,f) cache(p,n,f)
#define pciusbXHCIDebugTRBV(...) ((void)0)
#define pciusbXHCIDebugRIO(...) ((void)0)
#define pciusbError(...) ((void)0)
#include "xhci_ring.c"

static struct PCIController hc;
static struct pciusbXHCIDevice dev;
static struct pciusbXHCIIODevPrivate priv;
static struct IOUsbHWReq io;
static struct pcisusbXHCIRing ring;
static struct IORequest *owners[8];
static UBYTE contexts[64*MAX_DEVENDPOINTS];
static struct xhci_ep *ep;
static LONG stopResult,setResult;
static ULONG expectedStopType,afterState;
static UQUAD stoppedDMA;
static IPTR setParameter;
static BOOL corruptOwner;
static struct timerequest *timer=(struct timerequest *)(IPTR)0x1234;
static LONG xhciCmdSubmit(struct PCIController *h,APTR parameter,ULONG flags,ULONG *out,
                          struct timerequest *t)
{
    assert(h==&hc && t==timer && !out && !depth);
    assert((flags>>24)==dev.dc_SlotID && ((flags>>16)&31)==priv.dpEPID);
    ULONG type=flags&TRB_FLAG_TYPE_MASK;
    ++commands;
    if(type==TRBF_FLAG_CRTYPE_SET_TR_DEQUEUE_PTR) {
        assert(commands==2 && writes && !setParameter);
        setParameter=(IPTR)parameter;
        for(unsigned i=0;i<7;++i)if(owners[i]==&io.iouh_Req)
            assert((AROS_LE2LONG(ring.ring[i].flags)&TRB_FLAG_TYPE_MASK)==TRBF_FLAG_TRTYPE_NOOP);
        if(corruptOwner)owners[priv.dpSTRB]=NULL;
        return setResult;
    }
    assert(type==expectedStopType && commands==1 && !writes && !parameter);
    assert(!(flags&((1U<<23)|(1U<<9)))); /* no suspend / preserve flag */
    ep->ctx[0]=AROS_LONG2LE(afterState);
    ep->deq.addr_lo=AROS_LONG2LE((ULONG)stoppedDMA);
    ep->deq.addr_hi=AROS_LONG2LE((ULONG)(stoppedDMA>>32));
    return stopResult;
}
static LONG xhciCmdEndpointStop(struct PCIController *h,ULONG slot,ULONG epid,ULONG suspend,struct timerequest *t)
{ return xhciCmdSubmit(h,NULL,(slot<<24)|(epid<<16)|(suspend<<23)|TRBF_FLAG_CRTYPE_STOP_ENDPOINT,NULL,t); }
static LONG xhciCmdEndpointReset(struct PCIController *h,ULONG slot,ULONG epid,ULONG preserve,struct timerequest *t)
{ return xhciCmdSubmit(h,NULL,(slot<<24)|(epid<<16)|(preserve<<9)|TRBF_FLAG_CRTYPE_RESET_ENDPOINT,NULL,t); }
#include "xhci_cancel.c"

static void setup(unsigned first,unsigned count,unsigned stop,unsigned cycle,unsigned state,unsigned ctx64)
{
    memset(&hc,0,sizeof(hc));memset(&dev,0,sizeof(dev));memset(&io,0,sizeof(io));
    memset(&priv,0,sizeof(priv));memset(&ring,0,sizeof(ring));memset(contexts,0,sizeof(contexts));
    hc.hc_Flags=ctx64?HCF_CTX64:0;dev.dc_SlotID=5;
    priv.dpDevice=&dev;priv.dpEPID=3;priv.dpSTRB=priv.dpTxSTRB=first;
    priv.dpSttTRB=priv.dpTxETRB=(first+count-1)%7;
    io.iouh_DriverPrivate1=&priv;io.iouh_Req.io_Command=UHCMD_CONTROLXFER;
    io.iouh_Data=(APTR)(IPTR)0xfeed0000;io.iouh_Length=512;io.iouh_Actual=17;io.iouh_Req.io_Error=IOERR_ABORTED;
    dev.dc_EPAllocs[3]=(struct allocation){&ring,(APTR)(IPTR)0x123456780ULL};
    dev.dc_SlotCtx.dmaa_Ptr=contexts;ep=(struct xhci_ep *)(contexts+3*(ctx64?64:32));
    ep->ctx[0]=AROS_LONG2LE(state);ring.ringio=owners;ring.end=RINGENDCFLAG|5;
    for(unsigned i=0;i<8;++i) {
        owners[i]=(struct IORequest *)(IPTR)0x9999;
        ring.ring[i].flags=AROS_LONG2LE(TRBF_FLAG_TRTYPE_NORMAL|TRBF_FLAG_C);
        ring.ring[i].tparams=AROS_LONG2LE(42);
    }
    ring.ring[7].flags=AROS_LONG2LE(TRBF_FLAG_TRTYPE_LINK|TRBF_FLAG_C|TRBF_FLAG_TC);
    for(unsigned n=0;n<count;++n)owners[(first+n)%7]=&io.iouh_Req;
    unsigned current=stop==7?0:stop,normalizedCycle=stop==7?!cycle:cycle;
    ring.ring[current].flags=AROS_LONG2LE(TRBF_FLAG_TRTYPE_NORMAL|(normalizedCycle?TRBF_FLAG_C:0));
    stoppedDMA=(UQUAD)(IPTR)dev.dc_EPAllocs[3].dmaa_DMA+stop*sizeof(struct xhci_trb)+cycle;
    stopResult=setResult=TRB_CC_SUCCESS;afterState=3;
    expectedStopType=state==2?TRBF_FLAG_CRTYPE_RESET_ENDPOINT:TRBF_FLAG_CRTYPE_STOP_ENDPOINT;
    depth=writes=invalidates=commands=0;setParameter=0;corruptOwner=FALSE;
}
static void successful_cases(void)
{
    unsigned cases=0;
    for(unsigned first=0;first<7;++first)for(unsigned count=1;count<7;++count)
    for(unsigned stop=0;stop<8;++stop)for(unsigned cycle=0;cycle<2;++cycle)
    for(unsigned state=1;state<=3;++state)for(unsigned ctx64=0;ctx64<2;++ctx64) {
        setup(first,count,stop,cycle,state,ctx64);
        /* Cover both control and data range selection. */
        if(ctx64)io.iouh_Req.io_Command=UHCMD_BULKXFER;
        struct IOUsbHWReq savedIO=io;struct pciusbXHCIIODevPrivate savedPriv=priv;
        struct xhci_trb before[8];memcpy(before,ring.ring,sizeof(before));
        BOOL targeted[8]={0};for(unsigned n=0;n<count;++n)targeted[(first+n)%7]=TRUE;
        unsigned current=stop==7?0:stop;BOOL moves=targeted[current];
        assert(xhciRetireCancelledRequest(&hc,&io,timer));
        assert(commands==(moves?2:1) && invalidates==2 && writes==count && !depth);
        for(unsigned i=0;i<8;++i) {
            if(targeted[i])assert(!owners[i]);
            else { assert(owners[i]==(struct IORequest *)(IPTR)0x9999);assert(!memcmp(&before[i],&ring.ring[i],sizeof(before[i]))); }
        }
        unsigned next=(first+count)%7;
        if(moves) {
            /* Independent walk through the physical Link counts DCS toggles. */
            unsigned pos=stop,c=cycle;
            if(pos==7){pos=0;c=!c;}
            do { ++pos;if(pos==7){pos=0;c=!c;} } while(pos!=next);
            assert(setParameter==((IPTR)dev.dc_EPAllocs[3].dmaa_DMA+next*16+c));
            assert(ring.end==(RINGENDCFLAG|next));
        } else { assert(!setParameter && ring.end==(RINGENDCFLAG|5)); }
        assert(!memcmp(&io,&savedIO,sizeof(io)) && !memcmp(&priv,&savedPriv,sizeof(priv)));
        ++cases;
    }
    printf("xhci-cancel-command result=PASS cases=%u stopped-context,raw-dma,cycle,control,data,32/64-contexts\n",cases);
}
static void failures(void)
{
    /* Stop and dequeue failures must never authorize client release. */
    LONG failures[]={-1,TRB_CC_CONTEXT_STATE_ERROR,TRB_CC_TRB_ERROR};
    for(unsigned n=0;n<sizeof(failures)/sizeof(*failures);++n) {
        setup(2,3,3,1,1,0);stopResult=failures[n];
        assert(!xhciRetireCancelledRequest(&hc,&io,timer));
        assert(commands==1 && !writes && owners[2]==&io.iouh_Req && owners[4]==&io.iouh_Req);
        setup(2,3,3,1,1,0);setResult=failures[n];
        assert(!xhciRetireCancelledRequest(&hc,&io,timer));
        assert(commands==2 && writes==3 && owners[2]==&io.iouh_Req && owners[4]==&io.iouh_Req);
        assert(ring.end==(RINGENDCFLAG|5));
    }
    for(unsigned state=0;state<8;++state)if(state!=3) {
        setup(2,3,3,1,1,0);afterState=state;
        assert(!xhciRetireCancelledRequest(&hc,&io,timer));assert(!writes && commands==1);
    }
    for(unsigned bad=0;bad<12;++bad) {
        setup(2,3,3,1,1,0);
        switch(bad) {
        case 0:stoppedDMA=(IPTR)dev.dc_EPAllocs[3].dmaa_DMA-16;break;
        case 1:stoppedDMA=(IPTR)dev.dc_EPAllocs[3].dmaa_DMA+128;break;
        case 2:stoppedDMA|=2;break;
        case 3:priv.dpEPID=MAX_DEVENDPOINTS;break;
        case 4:dev.dc_SlotID=0;break;
        case 5:dev.dc_EPAllocs[3].dmaa_DMA=(APTR)(IPTR)0xfffffffffffffff0ULL;break;
        case 6:dev.dc_EPAllocs[3].dmaa_DMA=(APTR)(IPTR)0x123;break;
        case 7:priv.dpSTRB=7;break;
        case 8:priv.dpSttTRB=7;break;
        case 9:owners[4]=NULL;break;
        case 10:ep->ctx[0]=0;break;
        case 11:ep->ctx[0]=AROS_LONG2LE(4);break;
        }
        assert(!xhciRetireCancelledRequest(&hc,&io,timer));assert(!writes);
    }
    setup(2,3,3,1,1,0);corruptOwner=TRUE;
    assert(!xhciRetireCancelledRequest(&hc,&io,timer));
    assert(owners[3]==&io.iouh_Req && owners[4]==&io.iouh_Req);
    setup(2,3,3,1,1,0);
    assert(!xhciRetireCancelledRequest(NULL,&io,timer));
    assert(!xhciRetireCancelledRequest(&hc,NULL,timer));
    assert(!xhciRetireCancelledRequest(&hc,&io,NULL));
    io.iouh_DriverPrivate1=NULL;assert(!xhciRetireCancelledRequest(&hc,&io,timer));
    assert(!commands && !writes);
    puts("xhci-cancel-failures result=PASS command-timeout,error,state,address,ownership,null");
}
int main(void) { successful_cases();failures();return 0; }
