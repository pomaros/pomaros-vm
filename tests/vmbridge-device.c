// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/errors.h>
static void bridge_mark(const char *s) { while(*s)RawPutChar(*s++); }
static LONG complete(struct IOStdReq *r)
{
    for(unsigned tick=0;tick<250;++tick) {
        if(CheckIO((struct IORequest *)r))return WaitIO((struct IORequest *)r);
        Delay(1);
    }
    bridge_mark("[VM] vmbridge request timeout FAIL\n");
    for(;;)Wait(0); /* retain caller buffers until external supervisor stops */
}
static UBYTE pattern(unsigned session,unsigned offset)
{ return (UBYTE)(offset*37+offset/251+session*17); }
int test_vmbridge_device(void)
{
    BPTR lock=Lock((CONST_STRPTR)"DEVS:vmbridge.device",ACCESS_READ);
    if(!lock)return -1;UnLock(lock);
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *r=port?(APTR)CreateIORequest(port,sizeof(*r)):NULL;
    UBYTE data[997];int opened=0,okay=0;
    if(!r)goto done;
    for(unsigned session=0;session<2;++session) {
        if(OpenDevice((CONST_STRPTR)"vmbridge.device",4050,(struct IORequest *)r,0))goto done;
        opened=1;
        r->io_Command=CMD_READ;r->io_Data=data;r->io_Length=sizeof(data);
        SendIO((struct IORequest *)r);Delay(1);
        if(CheckIO((struct IORequest *)r)) { complete(r);goto done; }
        AbortIO((struct IORequest *)r);if(complete(r)!=IOERR_ABORTED)goto done;
        for(unsigned sent=0;sent<65536;) {
            unsigned n=65536-sent;if(n>sizeof(data))n=sizeof(data);
            for(unsigned i=0;i<n;++i)data[i]=pattern(session,sent+i);
            r->io_Command=CMD_WRITE;r->io_Data=data;r->io_Length=n;
            SendIO((struct IORequest *)r);
            if(complete(r) || !r->io_Actual || r->io_Actual>n)goto done;
            sent+=r->io_Actual;
        }
        r->io_Command=CMD_UPDATE;r->io_Length=0;
        SendIO((struct IORequest *)r);if(complete(r))goto done;
        for(unsigned received=0;received<65536;) {
            r->io_Command=CMD_READ;r->io_Data=data;r->io_Length=sizeof(data);
            SendIO((struct IORequest *)r);
            if(complete(r) || !r->io_Actual || r->io_Actual>sizeof(data) || r->io_Actual>65536-received)goto done;
            for(unsigned i=0;i<r->io_Actual;++i)if(data[i]!=pattern(session,received+i))goto done;
            received+=r->io_Actual;
        }
        CloseDevice((struct IORequest *)r);opened=0;
    }
    okay=1;
done:
    if(opened)CloseDevice((struct IORequest *)r);
    if(r)DeleteIORequest((struct IORequest *)r);if(port)DeleteMsgPort(port);
    bridge_mark(okay?"[VM] vmbridge read write abort reopen PASS\n":"[VM] vmbridge read write abort reopen FAIL\n");
    return okay;
}
