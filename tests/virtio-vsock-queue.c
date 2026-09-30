// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _AROS_TYPES_TIMEVAL_S_H_
#define __NOLIBBASE__
#include <aros/symbolsets.h>
#undef AROS_LIBREQ
#define AROS_LIBREQ(base,version)
extern struct ExecBase *SysBase;
#include "vsock_transport.c"
static struct VirtioVsock net;
static struct virtio_queue rx,tx,eventq;
static UWORD notify[3];
static void init(UWORD index)
{
    memset(&net,0,sizeof(net));memset(&rx,0,sizeof(rx));memset(&tx,0,sizeof(tx));memset(&eventq,0,sizeof(eventq));
    net.ready=TRUE;net.cid=42;net.rx=&rx;net.tx=&tx;net.event=&eventq;net.bus=0x12340000;
    net.memory=calloc(1,9*4096+4);
    struct virtio_queue *qs[]={&rx,&tx,&eventq};
    for(unsigned i=0;i<3;++i) {
        struct virtio_queue *q=qs[i];
        q->q_Desc=calloc(8,sizeof(*q->q_Desc));
        q->q_Avail=calloc(1,64);q->q_Used=calloc(1,128);
        q->q_Index=i;q->q_Size=8;q->q_Notify=&notify[i];
        q->q_LastUsedIdx=q->q_Used->idx=q->q_Avail->idx=index;
    }
}
static void finish(void)
{
    free(net.memory);struct virtio_queue *qs[]={&rx,&tx,&eventq};
    for(unsigned i=0;i<3;++i) {
        free((void *)qs[i]->q_Desc);free((void *)qs[i]->q_Avail);free((void *)qs[i]->q_Used);
    }
}
static void receive(UWORD id,ULONG length)
{
    rx.q_Used->ring[rx.q_Used->idx%8].id=id;
    rx.q_Used->ring[rx.q_Used->idx%8].len=length;++rx.q_Used->idx;
}
int main(void)
{
    UBYTE packet[4096],copy[4096];unsigned cases=0;
    const UWORD starts[]={0,7,8,65534,65535};
    for(unsigned n=0;n<sizeof(starts)/sizeof(starts[0]);++n) {
        init(starts[n]);memset(packet,0xa5,sizeof(packet));
        struct VirtioVsockHeader h={0};h.src_cid=42;h.dst_cid=2;h.len=sizeof(packet)-sizeof(h);
        memcpy(packet,&h,sizeof(h));
        assert(VirtioVsockSend(&net,packet,sizeof(packet))==1);
        assert(tx.q_Desc[0].addr==net.bus+8*4096 && tx.q_Desc[0].len==4096);
        packet[100]=0x5a;assert(net.memory[8*4096+100]==0xa5);
        assert(VirtioVsockSend(&net,packet,sizeof(packet))==0);
        h.src_cid=2;h.dst_cid=42;memcpy(packet,&h,sizeof(h));
        for(unsigned i=0;i<8;++i) { memcpy(net.memory+i*4096,packet,sizeof(packet));receive(i,4096); }
        /* RX must progress even with a full TX queue (flow-control deadlock). */
        for(unsigned i=0;i<8;++i) {
            assert(VirtioVsockReceive(&net,copy,sizeof(copy))==4096);
            assert(!memcmp(copy,packet,sizeof(copy)));
        }
        assert(VirtioVsockReceive(&net,copy,sizeof(copy))==0);
        tx.q_Used->ring[tx.q_LastUsedIdx%8].id=0;tx.q_Used->ring[tx.q_LastUsedIdx%8].len=0;++tx.q_Used->idx;
        assert(VirtioVsockPollTX(&net)==1 && !net.tx_pending);
        finish();++cases;
    }
    for(unsigned bad=0;bad<8;++bad) {
        init(65535);struct VirtioVsockHeader h={0};h.src_cid=2;h.dst_cid=42;
        if(bad==3)h.len=1;
        if(bad==4)h.dst_cid=43;
        if(bad==5)h.src_cid=1ULL<<32;
        memcpy(net.memory,&h,sizeof(h));
        receive(bad==0?8:0,bad==1?43:bad==2?4097:44);
        if(bad==6)receive(0,44);
        if(bad==7)rx.q_Used->idx=(UWORD)(rx.q_LastUsedIdx+9);
        assert(VirtioVsockReceive(&net,copy,sizeof(copy))==-1 && net.failed);
        assert(rx.q_Avail->idx==65535);finish();++cases;
    }
    init(0);eventq.q_Used->idx=1;
    assert(VirtioVsockPollTX(&net)==-1 && net.failed);
    assert(VirtioVsockReceive(&net,copy,sizeof(copy))==-1);finish();++cases;
    for(unsigned bad=0;bad<4;++bad) {
        init(0);struct VirtioVsockHeader h={0};h.src_cid=42;h.dst_cid=2;memcpy(packet,&h,sizeof(h));
        assert(VirtioVsockSend(&net,packet,44)==1);
        tx.q_Used->idx=bad==0?2:1;tx.q_Used->ring[0].id=bad==1?1:0;
        tx.q_Used->ring[0].len=bad==2?1:0;if(bad==3)net.tx_pending=FALSE;
        assert(VirtioVsockPollTX(&net)==-1 && net.failed);finish();++cases;
    }
    printf("virtio-vsock-queue result=PASS cases=%u owned-TX,concurrent-RX,wrap,malformed,reset\n",cases);
}
