// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vsock_stream.h"
static struct VsockStream s;
static unsigned calls;static int busy;static uint8_t output[4096];static size_t output_size;
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void set32(uint8_t *p,uint32_t x)
{ p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24; }
static int emit(void *context,const uint8_t *p,size_t n)
{ (void)context;++calls;if(busy)return 0;memcpy(output,p,n);output_size=n;return 1; }
static void packet(uint8_t *p,unsigned op,uint32_t credit,uint32_t fwd,unsigned bytes)
{
    memset(p,0,44+bytes);p[0]=2;p[8]=42;set32(p+16,4050);set32(p+20,49152);
    set32(p+24,bytes);p[28]=1;p[30]=op;set32(p+36,credit);set32(p+40,fwd);
}
static void connect_stream(uint32_t credit)
{
    uint8_t p[44];assert(VsockStreamInit(&s,42,49152,2,4050));busy=1;
    assert(VsockStreamPump(&s,emit,NULL)==0 && s.request);
    busy=0;assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==1 && !s.request);
    packet(p,2,credit,0,0);assert(VsockStreamFeed(&s,p,44)==1 && s.state==VS_OPEN);
}
int main(void)
{
    uint8_t p[4096],data[20000],copy[20000];memset(data,0xa5,sizeof(data));unsigned cases=0;
    connect_stream(7);assert(VsockStreamWrite(&s,data,100)==100);
    busy=1;assert(VsockStreamPump(&s,emit,NULL)==0 && s.tx_sent==0 && s.tx_count==100);
    busy=0;assert(VsockStreamPump(&s,emit,NULL)==1 && output_size==51 && u32(output+24)==7 && s.tx_count==93);
    assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==7);
    assert(VsockStreamPump(&s,emit,NULL)==0); /* no unbounded query storm */
    packet(p,6,100,7,0);assert(VsockStreamFeed(&s,p,44)==1);
    assert(VsockStreamPump(&s,emit,NULL)==1 && u32(output+24)==93 && s.tx_sent==100);++cases;
    connect_stream(65536);assert(VsockStreamWrite(&s,data,sizeof(data))==16384);
    unsigned total=0,frames=0;while(s.tx_count) {
        assert(VsockStreamPump(&s,emit,NULL)==1 && output_size<=4096);
        total+=u32(output+24);++frames;
    }
    assert(total==16384 && frames==5);++cases;
    for(unsigned i=0;i<4;++i) {
        packet(p,5,65536,s.tx_sent,4052);memset(p+44,i,4052);
        assert(VsockStreamFeed(&s,p,4096)==1);
    }
    packet(p,5,65536,s.tx_sent,176);assert(VsockStreamFeed(&s,p,220)==1);
    assert(s.rx_count==16384 && s.rx_fwd==0);
    assert(VsockStreamRead(&s,copy,997)==997 && s.rx_fwd==997);
    for(unsigned i=0;i<997;++i)assert(copy[i]==0);
    assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==6 && u32(output+40)==997);
    assert(VsockStreamRead(&s,copy,sizeof(copy))==15387 && !s.rx_count);++cases;
    connect_stream(100);s.tx_sent=s.peer_fwd=UINT32_MAX-4;
    assert(VsockStreamWrite(&s,data,10)==10 && VsockStreamPump(&s,emit,NULL)==1 && s.tx_sent==5);
    packet(p,6,100,5,0);assert(VsockStreamFeed(&s,p,44)==1 && s.peer_fwd==5);++cases;
    connect_stream(100);packet(p,6,100,1,0);assert(!VsockStreamFeed(&s,p,44) && s.state==VS_FAILED);
    assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==3 && s.failed);++cases;
    for(unsigned bad=0;bad<6;++bad) {
        connect_stream(100);packet(p,5,100,0,1);
        if(bad==0)p[28]=99;
        if(bad==1)p[24]=2;
        if(bad==2)p[32]=1;
        if(bad==3)s.rx_count=16384;
        if(bad==4)p[30]=1;
        if(bad==5)p[30]=2;
        assert(!VsockStreamFeed(&s,p,45) && s.reset);++cases;
    }
    connect_stream(100);packet(p,5,100,0,1);p[20]=99;
    assert(VsockStreamFeed(&s,p,45)==-1 && s.state==VS_OPEN);++cases;
    connect_stream(100);assert(VsockStreamWrite(&s,data,10)==10);VsockStreamShutdown(&s);
    assert(!VsockStreamWrite(&s,data,1));
    assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==5);
    assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==4 && u32(output+32)==3);
    packet(p,3,0,0,0);assert(VsockStreamFeed(&s,p,44)==1 && s.state==VS_CLOSED);++cases;
    connect_stream(100);packet(p,4,100,0,0);p[32]=1;
    assert(VsockStreamFeed(&s,p,44)==1 && !VsockStreamWrite(&s,data,1));
    p[32]=2;assert(VsockStreamFeed(&s,p,44)==1 && s.reset);
    assert(VsockStreamPump(&s,emit,NULL)==1 && output[30]==3 && s.state==VS_CLOSED);++cases;
    connect_stream(100);packet(p,5,100,0,10);memset(p+44,0x5a,10);
    assert(VsockStreamFeed(&s,p,54)==1);s.rx_fwd=UINT32_MAX-5;
    packet(p,3,0,0,0);assert(VsockStreamFeed(&s,p,44)==1);
    assert(VsockStreamRead(&s,copy,10)==10 && s.rx_fwd==4 && s.state==VS_CLOSED);
    for(unsigned i=0;i<10;++i)assert(copy[i]==0x5a);++cases;
    printf("vsock-stream result=PASS cases=%u credits,fragmentation,backpressure,wrap,shutdown,malformed\n",cases);
}
