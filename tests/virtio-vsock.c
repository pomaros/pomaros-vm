// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "vsock_transport.c"
static void socket_mark(const char *s) { while(*s)RawPutChar(*s++); }
/* One bounded stream with 32 bytes of receive credit. The production owner
 * will supply stream buffering, reconnection and service framing separately. */
int test_vsock(OOP_Object *object,struct Library *oop)
{
    struct VirtioVsock *v=NULL;int okay=0;
    if(!VirtioVsockOpen(oop,object,&v))goto done;
    UBYTE packet[4096],challenge[32],reply[32];unsigned received=0;
    UQUAD counter;__asm__ volatile("mrs %0,cntvct_el0":"=r"(counter));
    for(unsigned i=0;i<32;++i)challenge[i]=(UBYTE)(counter>>(8*(i%8)))^(UBYTE)(i*37);
    struct VirtioVsockHeader h={0};
    h.src_cid=VirtioVsockCID(v);h.dst_cid=2;h.src_port=49152;h.dst_port=4050;
    h.type=1;h.op=1;h.buf_alloc=32;
    memcpy(packet,&h,sizeof(h));
    if(VirtioVsockSend(v,packet,sizeof(h))!=1)goto done;
    BOOL connected=FALSE,sent=FALSE;ULONG peer_credit=0;unsigned control=0;
    for(unsigned tick=0;tick<250;++tick) {
        LONG n=VirtioVsockReceive(v,packet,sizeof(packet));
        if(n<0)goto done;
        if(n) {
            struct VirtioVsockHeader r;memcpy(&r,packet,sizeof(r));
            if(r.src_cid!=2 || r.src_port!=4050 || r.dst_port!=49152 || r.type!=1)goto done;
            if(r.op==2 && !connected && !r.len) { connected=TRUE;peer_credit=r.buf_alloc; }
            else if(r.op==5 && connected && sent && r.len<=32-received) {
                memcpy(reply+received,packet+sizeof(r),r.len);received+=r.len;
                if(received==32) { okay=!memcmp(reply,challenge,32);break; }
            } else if(r.op==6 && !r.len)peer_credit=r.buf_alloc;
            else if(r.op==7 && !r.len)control=6;
            else goto done;
        }
        LONG ready=VirtioVsockPollTX(v);if(ready<0)goto done;
        if(ready && control) {
            h.op=control;h.len=0;memcpy(packet,&h,sizeof(h));
            if(VirtioVsockSend(v,packet,sizeof(h))!=1)goto done;control=0;
        } else if(ready && connected && !sent && peer_credit>=32) {
            h.op=5;h.len=32;memcpy(packet,&h,sizeof(h));memcpy(packet+sizeof(h),challenge,32);
            if(VirtioVsockSend(v,packet,sizeof(h)+32)!=1)goto done;sent=TRUE;
        }
        Delay(1);
    }
    /* Explicit reset ends this qualification flow before transport reset. */
    if(okay) {
        for(unsigned tick=0;tick<50 && VirtioVsockPollTX(v)==0;++tick)Delay(1);
        h.op=3;h.len=0;h.fwd_cnt=received;memcpy(packet,&h,sizeof(h));
        if(VirtioVsockSend(v,packet,sizeof(h))!=1)okay=0;
        for(unsigned tick=0;tick<50 && VirtioVsockPollTX(v)==0;++tick)Delay(1);
        if(VirtioVsockPollTX(v)!=1)okay=0;
    }
done:
    if(!VirtioVsockClose(v)) {
        socket_mark("[VM] vsock DMA quarantine FAIL\n");
        /* Code/context remain resident until the external deadline stops VM. */
        for(;;)Delay(50);
    }
    socket_mark(okay?"[VM] vsock host stream exchange PASS\n":"[VM] vsock host stream exchange FAIL\n");
    return okay;
}
