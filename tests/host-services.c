// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "client.c"
static void services_mark(const char *s) { while(*s)RawPutChar(*s++); }
static UBYTE fixture(unsigned offset) { return (UBYTE)(offset*37+offset/251); }
int test_host_services(void)
{
    struct VMExchangeClient c;int okay=0;UBYTE data[4096];ULONG got=0;
    const char *name="vmbridge-qualification.bin";
    if(!VMExchangeOpen(&c))goto done;
    if(!VMExchangeRequest(&c,1,NULL,0,data,sizeof(data),&got) || got!=sizeof("Macaros host services 1")-1 || memcmp(data,"Macaros host services 1",sizeof("Macaros host services 1")-1))goto close;
    if(VMExchangeRequest(&c,5,"../outside",10,data,sizeof(data),&got) || c.status!=1)goto close;
    if(!VMExchangeRequest(&c,2,name,strlen(name),NULL,0,&got))goto close;
    for(unsigned offset=0;offset<65536;offset+=sizeof(data)) {
        for(unsigned i=0;i<sizeof(data);++i)data[i]=fixture(offset+i);
        if(!VMExchangeRequest(&c,3,data,sizeof(data),NULL,0,&got))goto close;
    }
    if(!VMExchangeRequest(&c,4,NULL,0,data,8,&got) || got!=8 || le32(data)!=65536 || le32(data+4))goto close;
    /* A second upload cannot replace the published file. */
    if(!VMExchangeRequest(&c,2,name,strlen(name),NULL,0,&got) ||
       !VMExchangeRequest(&c,3,"wrong",5,NULL,0,&got))goto close;
    if(VMExchangeRequest(&c,4,NULL,0,data,8,&got) || c.status!=4)goto close;
    if(!VMExchangeRequest(&c,5,name,strlen(name),data,8,&got) || got!=8 || le32(data)!=65536 || le32(data+4))goto close;
    for(unsigned offset=0;offset<65536;) {
        UBYTE request[4]={0,16,0,0};
        if(!VMExchangeRequest(&c,6,request,4,data,sizeof(data),&got) || !got || got>65536-offset)goto close;
        for(unsigned i=0;i<got;++i)if(data[i]!=fixture(offset+i))goto close;
        offset+=got;
    }
    if(!VMExchangeRequest(&c,7,NULL,0,NULL,0,&got))goto close;
    okay=1;
close:VMExchangeClose(&c);
    if(okay) {
        services_mark("[VM] VMExchange LoadSeg begin\n");
        BPTR program=LoadSeg((CONST_STRPTR)"SYS:C/VMExchange");
        services_mark(program?"[VM] VMExchange LoadSeg ready\n":"[VM] VMExchange LoadSeg FAIL\n");
        UBYTE arguments[]="HELLO\n";
        okay=program && RunCommand(program,65536,arguments,6)==0;
        if(program)UnLoadSeg(program);
        if(okay)services_mark("[VM] VMExchange command hello PASS\n");
    }
done:services_mark(okay?"[VM] host file put get containment PASS\n":"[VM] host file put get containment FAIL\n");return okay;
}
