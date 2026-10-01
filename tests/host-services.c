// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "client.c"
static void services_mark(const char *s) { while(*s)RawPutChar(*s++); }
static UBYTE fixture(unsigned offset) { return (UBYTE)(offset*37+offset/251); }
static int run_command(UBYTE *arguments,LONG expected)
{
    BPTR program=LoadSeg((CONST_STRPTR)"SYS:C/VMExchange");
    if(!program)return 0;
    LONG result=RunCommand(program,65536,arguments,strlen((char *)arguments));
    UnLoadSeg(program);
    return result==expected;
}
static int command_files(void)
{
    BPTR request=Lock((CONST_STRPTR)"SYS:FILECOMMANDS.REQ",ACCESS_READ);
    if(!request)return 1;
    UnLock(request);
    /* The preceding filesystem qualification left this isolated volume
       inhibited. Reattach it before exercising the actual DOS CLI paths. */
    if(!Inhibit((CONST_STRPTR)"VMTEST0:",DOSFALSE))return 0;
    const char *destination="VMTEST0:EXCHANGE.BIN";
    BPTR prior=Lock((CONST_STRPTR)destination,ACCESS_READ);
    if(prior) { UnLock(prior);destination="VMTEST0:EXCHANG2.BIN"; }
    UBYTE put[]="PUT VMTEST0:DATA/KEPT.BIN command-upload.bin\n";
    UBYTE get[128];
    __builtin_snprintf((char *)get,sizeof(get),"GET command-upload.bin %s\n",destination);
    int okay=run_command(put,0) && run_command(get,0) && run_command(get,20);
    BPTR file=okay?Open((CONST_STRPTR)destination,MODE_OLDFILE):0;
    if(!file)okay=0;
    if(file) {
        UBYTE data[4096];
        for(unsigned offset=0;okay && offset<65536;offset+=sizeof(data)) {
            if(Read(file,data,sizeof(data))!=sizeof(data)) { okay=0;break; }
            for(unsigned i=0;i<sizeof(data);++i) {
                unsigned at=offset+i;
                if(data[i]!=(UBYTE)((at*37+11)^(at>>8))) { okay=0;break; }
            }
        }
        if(okay && Read(file,data,1)!=0)okay=0;
        if(!Close(file))okay=0;
    }
    if(!Inhibit((CONST_STRPTR)"VMTEST0:",DOSTRUE))okay=0;
    if(okay)services_mark("[VM] VMExchange CLI put get no-replace PASS\n");
    return okay;
}
int test_clipboard_sync(void);
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
        UBYTE arguments[]="HELLO\n";
        okay=run_command(arguments,0);
        if(okay)services_mark("[VM] VMExchange command hello PASS\n");
        if(okay)okay=command_files();
        if(okay)okay=test_clipboard_sync();
    }
done:services_mark(okay?"[VM] host file put get containment PASS\n":"[VM] host file put get containment FAIL\n");return okay;
}
