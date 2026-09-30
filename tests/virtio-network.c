// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
/* Same transport intended for the resident SANA-II driver. Shared hw/queue
 * objects are already linked by virtio-block-queue.c in this qualification. */
#include "net_transport.c"
static void net_mark(const char *s) { while(*s)RawPutChar(*s++); }
static unsigned be16(const UBYTE *p) { return ((unsigned)p[0]<<8)|p[1]; }
static void put16(UBYTE *p,unsigned n) { p[0]=n>>8;p[1]=n; }
static unsigned ip_checksum(const UBYTE *p,unsigned bytes)
{
    unsigned sum=0;
    for(unsigned i=0;i<bytes;i+=2)sum+=be16(p+i);
    while(sum>>16)sum=(sum&65535)+(sum>>16);
    return (~sum)&65535;
}
static int offered(const UBYTE *frame,unsigned length,const UBYTE *mac,const UBYTE *xid)
{
    if(length<14+20+8+240 || be16(frame+12)!=0x800)return 0;
    const UBYTE *ip=frame+14;
    unsigned ihl=(ip[0]&15)*4,total=be16(ip+2);
    if((ip[0]>>4)!=4 || ihl<20 || total<ihl+8+240 || total>length-14 ||
       ip[9]!=17 || (be16(ip+6)&0x3fff) || ip_checksum(ip,ihl))return 0;
    const UBYTE *udp=ip+ihl;
    unsigned bytes=be16(udp+4);
    if(be16(udp)!=67 || be16(udp+2)!=68 || bytes<8+240 || bytes>total-ihl)return 0;
    /* Without negotiated RX checksum offload, validate any supplied UDP
     * checksum ourselves. IPv4 permits a zero checksum. RFC 768. */
    if(be16(udp+6)) {
        unsigned sum=17+bytes;
        for(unsigned i=12;i<20;i+=2)sum+=be16(ip+i);
        for(unsigned i=0;i+1<bytes;i+=2)sum+=be16(udp+i);
        if(bytes&1)sum+=(unsigned)udp[bytes-1]<<8;
        while(sum>>16)sum=(sum&65535)+(sum>>16);
        if(sum!=65535)return 0;
    }
    const UBYTE *dhcp=udp+8;
    if(dhcp[0]!=2 || dhcp[1]!=1 || dhcp[2]!=6 || memcmp(dhcp+4,xid,4) ||
       memcmp(dhcp+28,mac,6) || memcmp(dhcp+236,"\x63\x82\x53\x63",4) ||
       !(dhcp[16]|dhcp[17]|dhcp[18]|dhcp[19]))return 0;
    int offer=0;
    for(unsigned i=240;i<bytes-8;) {
        unsigned tag=dhcp[i++];
        if(tag==255)return offer;
        if(tag==0)continue;
        if(i>=bytes-8)return 0;
        unsigned n=dhcp[i++];if(n>bytes-8-i)return 0;
        if(tag==53) { if(n!=1 || offer || dhcp[i]!=2)return 0;offer=1; }
        i+=n;
    }
    return 0;
}
struct NetIRQ { struct VirtioNet *net;volatile ULONG hits; };
static AROS_INTH1(network_irq,struct NetIRQ *,state)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;(void)__ufi_custom;(void)__ufi_code;(void)SysBase;
    (void)VirtioNetInterruptStatus(state->net);
    ++state->hits;
    return TRUE;
    AROS_INTFUNC_EXIT
}
int test_network_packets(OOP_Object *device,struct Library *oop,int irq_mode)
{
    struct VirtioNet *net=NULL;
    int okay=0;
    struct NetIRQ irq={0};
    struct Interrupt interrupt={0};
    interrupt.is_Node.ln_Type=NT_INTERRUPT;
    interrupt.is_Node.ln_Name="virtio-net qualification";
    interrupt.is_Data=&irq;interrupt.is_Code=(APTR)network_irq;
    if(!VirtioNetOpen(oop,device,&net)) { net_mark("[VM] virtio network open FAIL\n");
        goto done; }
    irq.net=net;
    if(irq_mode && !VirtioNetEnableInterrupt(net,&interrupt)) {
        net_mark("[VM] virtio network IRQ registration FAIL\n");goto done;
    }
    UBYTE mac[6],frame[342]={0},reply[1518];
    if(!VirtioNetMAC(net,mac))goto done;
    bug("[VM] virtio network MTU=%lu\n",(unsigned long)VirtioNetMTU(net));
    memset(frame,255,6);memcpy(frame+6,mac,6);put16(frame+12,0x800);
    UBYTE *ip=frame+14,*udp=ip+20,*dhcp=udp+8;
    ip[0]=0x45;put16(ip+2,sizeof(frame)-14);ip[8]=64;ip[9]=17;
    memset(ip+16,255,4);put16(ip+10,ip_checksum(ip,20));
    put16(udp,68);put16(udp+2,67);put16(udp+4,sizeof(frame)-14-20);
    dhcp[0]=1;dhcp[1]=1;dhcp[2]=6;
    /* Transaction tied to this guest's virtual counter; reject stale offers. */
    UQUAD counter;__asm__ volatile("mrs %0,cntvct_el0":"=r"(counter));
    for(unsigned i=0;i<4;++i)dhcp[4+i]=(UBYTE)(counter>>(8*i));
    put16(dhcp+10,0x8000);memcpy(dhcp+28,mac,6);
    memcpy(dhcp+236,"\x63\x82\x53\x63",4);
    dhcp[240]=53;dhcp[241]=1;dhcp[242]=1;dhcp[243]=255;
    if(VirtioNetSend(net,frame,sizeof(frame))!=1)goto done;
    for(unsigned tick=0;tick<250;++tick) {
        if(irq_mode && !irq.hits) { Delay(1);continue; }
        LONG sent=VirtioNetPollTX(net),received=VirtioNetReceive(net,reply,sizeof(reply));
        if(sent<0 || received<0) {
            ULONG badid=net->rx->q_Used->ring[net->rx->q_LastUsedIdx%8].id;
            UBYTE *badheader=net->memory+(badid<8?badid:0)*2048;
            bug("[VM] network completion sent=%ld received=%ld txidx=%u txlast=%u txid=%lu txlen=%lu rxidx=%u rxlast=%u rxid=%lu rxlen=%lu hdr=%u,%u,%u,%u\n",
                (long)sent,(long)received,net->tx->q_Used->idx,net->tx->q_LastUsedIdx,
                (unsigned long)net->tx->q_Used->ring[net->tx->q_LastUsedIdx%8].id,
                (unsigned long)net->tx->q_Used->ring[net->tx->q_LastUsedIdx%8].len,
                net->rx->q_Used->idx,net->rx->q_LastUsedIdx,
                (unsigned long)net->rx->q_Used->ring[net->rx->q_LastUsedIdx%8].id,
                (unsigned long)net->rx->q_Used->ring[net->rx->q_LastUsedIdx%8].len,
                badheader[0],badheader[1],badheader[10],badheader[11]);
            net_mark("[VM] virtio network completion FAIL\n");break;
        }
        if(received && offered(reply,received,mac,dhcp+4))okay=1;
        if(okay && sent==1)break;
        if(tick==249)okay=0;
        Delay(1);
    }
done:
    if(net && net->ready)bug("[VM] network summary irq=%lu tx=%u/%u rx=%u/%u failed=%u\n",
        (unsigned long)irq.hits,net->tx->q_Used->idx,net->tx->q_LastUsedIdx,
        net->rx->q_Used->idx,net->rx->q_LastUsedIdx,net->failed);
    if(!VirtioNetClose(net)) {
        net_mark("[VM] virtio network reset quarantine\n");
        /* Keep this command and its provider references alive until supervisor
         * termination; unloading code while reset failed is not safe. */
        for(;;)Wait(0);
    }
    if(okay && irq_mode && irq.hits)net_mark("[VM] virtio network interrupt delivery PASS\n");
    if(okay)net_mark("[VM] virtio network DHCP exchange PASS\n");
    else net_mark("[VM] virtio network DHCP exchange FAIL\n");
    return okay;
}
