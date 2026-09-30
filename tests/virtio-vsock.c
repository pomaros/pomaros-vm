// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <string.h>
#include "vsock_transport.c"
#include "vsock_stream.c"
static void socket_mark(const char *s) { while(*s)RawPutChar(*s++); }
static int emit_packet(void *context,const uint8_t *packet,size_t bytes)
{ return VirtioVsockSend(context,packet,bytes); }
static UBYTE pattern(UQUAD seed,unsigned offset)
{ return (UBYTE)(seed>>(8*(offset%8)))^(UBYTE)(offset*37+offset/251); }
int test_vsock(OOP_Object *object,struct Library *oop)
{
    struct VirtioVsock *v=NULL;struct VsockStream *s=NULL;int okay=0;
    if(!VirtioVsockOpen(oop,object,&v))goto done;
    s=AllocMem(sizeof(*s),MEMF_PUBLIC|MEMF_CLEAR);if(!s)goto done;
    for(unsigned session=0;session<2;++session) {
        UBYTE packet[4096],data[997];unsigned sent=0,received=0;
        UQUAD seed;__asm__ volatile("mrs %0,cntvct_el0":"=r"(seed));
        if(!VsockStreamInit(s,VirtioVsockCID(v),49152+session,2,4050))goto done;
        for(unsigned tick=0;tick<500;++tick) {
            /* Bound drain work and keep RX independent from TX backpressure. */
            for(unsigned batch=0;batch<8;++batch) {
                LONG n=VirtioVsockReceive(v,packet,sizeof(packet));if(n<0)goto done;
                if(!n)break;
                if(VsockStreamFeed(s,packet,n)!=1)goto done;
            }
            size_t n=VsockStreamRead(s,data,sizeof(data));
            if(n>65536-received)goto done;
            for(unsigned i=0;i<n;++i)if(data[i]!=pattern(seed,received+i))goto done;
            received+=n;
            if(sent<65536) {
                unsigned want=65536-sent;if(want>sizeof(data))want=sizeof(data);
                for(unsigned i=0;i<want;++i)data[i]=pattern(seed,sent+i);
                sent+=VsockStreamWrite(s,data,want);
            }
            if(sent==65536 && received==65536 && !s->tx_count)VsockStreamShutdown(s);
            if(VsockStreamPump(s,emit_packet,v)<0 || s->failed)goto done;
            /* A closed stream can still contain data already delivered by its peer. */
            if(s->state==VS_CLOSED && !s->rx_count)break;
            Delay(1);
        }
        if(sent!=65536 || received!=65536 || s->state!=VS_CLOSED) {
            bug("[VM] vsock incomplete sent=%u received=%u state=%u\n",sent,received,s->state);
            goto done;
        }
        for(unsigned tick=0;tick<50 && VirtioVsockPollTX(v)==0;++tick)Delay(1);
        if(VirtioVsockPollTX(v)!=1)goto done;
    }
    okay=1;
done:
    if(s)FreeMem(s,sizeof(*s));
    if(!VirtioVsockClose(v)) {
        socket_mark("[VM] vsock DMA quarantine FAIL\n");
        for(;;)Delay(50);
    }
    socket_mark(okay?"[VM] vsock two 64KiB streams PASS\n":"[VM] vsock two 64KiB streams FAIL\n");
    return okay;
}
