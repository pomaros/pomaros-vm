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
#include "net_transport.c"
static struct VirtioNet net;
static struct virtio_queue rx,tx;
static UWORD notify[2];
static void init(UWORD index)
{
    memset(&net,0,sizeof(net));memset(&rx,0,sizeof(rx));memset(&tx,0,sizeof(tx));
    net.ready=TRUE;net.mtu=1500;net.rx=&rx;net.tx=&tx;net.bus=0x12340000;
    net.memory=calloc(1,9*2048);
    struct virtio_queue *qs[]={&rx,&tx};
    for(unsigned i=0;i<2;++i) {
        struct virtio_queue *q=qs[i];
        q->q_Desc=calloc(8,sizeof(*q->q_Desc));
        q->q_Avail=calloc(1,64);q->q_Used=calloc(1,128);
        q->q_Index=i;q->q_Size=8;q->q_Notify=&notify[i];
        q->q_LastUsedIdx=q->q_Used->idx=q->q_Avail->idx=index;
    }
}
static void finish(void)
{
    free(net.memory);struct virtio_queue *qs[]={&rx,&tx};
    for(unsigned i=0;i<2;++i) {
        free((void *)qs[i]->q_Desc);free((void *)qs[i]->q_Avail);free((void *)qs[i]->q_Used);
    }
}
static void receive(UWORD id,ULONG length)
{
    rx.q_Used->ring[rx.q_Used->idx%8].id=id;
    rx.q_Used->ring[rx.q_Used->idx%8].len=length;
    ++rx.q_Used->idx;
}
int main(void)
{
    UBYTE frame[1518],copy[1518];memset(frame,0xa5,sizeof(frame));
    unsigned cases=0;
    const UWORD starts[]={0,7,8,65534,65535};
    for(unsigned n=0;n<sizeof(starts)/sizeof(starts[0]);++n) {
        init(starts[n]);
        assert(VirtioNetSend(&net,frame,sizeof(frame))==1);
        assert(tx.q_Desc[0].addr==net.bus+8*2048 && tx.q_Desc[0].len==1530);
        assert(!memcmp(net.memory+8*2048+12,frame,sizeof(frame)));
        memset(frame,0x5a,sizeof(frame));
        assert(net.memory[8*2048+12]==0xa5); /* caller memory was copied */
        assert(VirtioNetSend(&net,frame,sizeof(frame))==0);
        tx.q_Used->ring[tx.q_LastUsedIdx%8].id=0;
        tx.q_Used->ring[tx.q_LastUsedIdx%8].len=0;++tx.q_Used->idx;
        assert(VirtioNetPollTX(&net)==1 && !net.tx_pending);
        for(unsigned i=0;i<8;++i) {
            net.memory[i*2048]=i&1?6:0; /* ignored checksum/RSC hints */
            net.memory[i*2048+10]=i&1; /* VZ zero and standard one */
            memcpy(net.memory+i*2048+12,frame,sizeof(frame));receive(i,1530);
        }
        for(unsigned i=0;i<8;++i) {
            assert(VirtioNetReceive(&net,copy,sizeof(copy))==1518);
            assert(!memcmp(copy,frame,sizeof(copy)));
        }
        assert(VirtioNetReceive(&net,copy,sizeof(copy))==0);
        memset(frame,0xa5,sizeof(frame));finish();++cases;
    }
    init(0);net.mtu=68;
    assert(VirtioNetSend(&net,frame,87)==-1 && !net.tx_pending);
    assert(VirtioNetSend(&net,frame,86)==1);
    net.memory[10]=1;receive(0,12+87);
    assert(VirtioNetReceive(&net,copy,sizeof(copy))==-1 && net.failed);
    finish();++cases;
    for(unsigned bad=0;bad<8;++bad) {
        init(65535);net.memory[10]=1;
        receive(bad==0?8:0,bad==1?25:bad==2?1531:26);
        if(bad==3)net.memory[0]=1;
        if(bad==4)net.memory[1]=1;
        if(bad==5)net.memory[10]=2;
        if(bad==6)receive(0,26);
        if(bad==7)rx.q_Used->idx=(UWORD)(rx.q_LastUsedIdx+9);
        assert(VirtioNetReceive(&net,copy,sizeof(copy))==-1 && net.failed);
        assert(rx.q_Avail->idx==65535); /* never requeue a malformed batch */
        finish();++cases;
    }
    for(unsigned bad=0;bad<4;++bad) {
        init(0);assert(VirtioNetSend(&net,frame,14)==1);
        tx.q_Used->idx=bad==0?2:1;
        tx.q_Used->ring[0].id=bad==1?1:0;
        tx.q_Used->ring[0].len=bad==2?1:0;
        if(bad==3)net.tx_pending=FALSE;
        assert(VirtioNetPollTX(&net)==-1 && net.failed);
        finish();++cases;
    }
    printf("virtio-net-queue result=PASS cases=%u owned-TX,RX-batches,wrap,malformed-completions\n",cases);
}
