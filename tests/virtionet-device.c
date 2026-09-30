// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/sana2.h>
#include <exec/errors.h>
#include <string.h>

static BOOL copy_buffer(APTR to,APTR from,ULONG bytes)
{ memcpy(to,from,bytes);return TRUE; }
static void mark(const char *s) { while(*s)RawPutChar(*s++); }
/* On timeout keep request memory and callback code alive for the supervisor.
 * Open/Close are also bounded by the external VM deadline. */
static LONG completed(struct IOSana2Req *io)
{
    for(unsigned tick=0;tick<100;++tick) {
        if(CheckIO((struct IORequest *)io))return WaitIO((struct IORequest *)io);
        Delay(1);
    }
    mark("[VM] SANA request timeout FAIL\n");
    for(;;)Wait(0);
}
static LONG command(struct IOSana2Req *io,UWORD cmd)
{
    io->ios2_Req.io_Command=cmd;io->ios2_WireError=0;
    SendIO((struct IORequest *)io);return completed(io);
}
int test_virtionet_device(void)
{
    BPTR lock=Lock((CONST_STRPTR)"DEVS:Networks/virtionet.device",ACCESS_READ);
    if(!lock)return -1;
    UnLock(lock);
    struct MsgPort *port=CreateMsgPort();
    struct IOSana2Req *io=port?(APTR)CreateIORequest(port,sizeof(*io)):NULL;
    struct TagItem tags[]={{S2_CopyToBuff,(IPTR)copy_buffer},
        {S2_CopyFromBuff,(IPTR)copy_buffer},{TAG_DONE,0}};
    UBYTE buffer[1518];int opened=0,okay=0;
    if(!io)goto done;
    /* Two full ownership cycles exercise worker exit, PCI release and reset. */
    for(unsigned pass=0;pass<2;++pass) {
        io->ios2_BufferManagement=tags;
        if(OpenDevice((CONST_STRPTR)"networks/virtionet.device",0,(struct IORequest *)io,0))goto done;
        opened=1;
        struct Sana2DeviceQuery query={0};query.SizeAvailable=sizeof(query);
        io->ios2_StatData=&query;
        if(command(io,S2_DEVICEQUERY) || query.SizeSupplied!=sizeof(query) ||
           query.AddrFieldSize!=48 || query.HardwareType!=S2WireType_Ethernet ||
           query.MTU<576 || query.MTU>1500)goto done;
        if(command(io,S2_GETSTATIONADDRESS) || (io->ios2_SrcAddr[0]&1) ||
           memcmp(io->ios2_SrcAddr,io->ios2_DstAddr,6))goto done;
        if(command(io,S2_CONFIGINTERFACE) || command(io,S2_ONLINE))goto done;
        io->ios2_PacketType=0x88b5;io->ios2_Data=buffer;
        io->ios2_DataLength=sizeof(buffer);io->ios2_Req.io_Command=CMD_READ;
        SendIO((struct IORequest *)io);
        Delay(1);if(CheckIO((struct IORequest *)io)) { completed(io);goto done; }
        AbortIO((struct IORequest *)io);
        if(completed(io)!=IOERR_ABORTED)goto done;
        if(command(io,S2_OFFLINE))goto done;
        CloseDevice((struct IORequest *)io);opened=0;
    }
    okay=1;mark("[VM] SANA query configure abort reopen PASS\n");
done:
    if(opened)CloseDevice((struct IORequest *)io);
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    if(!okay)mark("[VM] SANA device lifecycle FAIL\n");
    return okay;
}
