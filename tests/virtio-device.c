// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <string.h>
#include <stdint.h>
static void mark(const char *p) { while(*p) RawPutChar(*p++); }
static void number(const char *name,uint64_t value)
{
    mark(name);mark("=0x");
    for(int shift=60;shift>=0;shift-=4) RawPutChar("0123456789abcdef"[(value>>shift)&15]);
    mark("\n");
}
static uint64_t ticks(void) { uint64_t t;__asm__ volatile("mrs %0,cntvct_el0":"=r"(t));return t; }
/* -1: optional package absent; 0: failed; 1: actual device read succeeded. */
int test_virtio_device(void)
{
    BPTR lock=Lock((CONST_STRPTR)"DEVS:virtio.device",ACCESS_READ);
    if(!lock) return -1;
    UnLock(lock);
    mark("[VM] full virtio.device load begin\n");
    /* Published classes and the driver's IO task live until VM shutdown. */
    struct Library *bus=OpenLibrary((CONST_STRPTR)"DEVS:Drivers/bus.hidd",0);
    struct Library *storage=OpenLibrary((CONST_STRPTR)"DEVS:Drivers/storage.hidd",0);
    if(!bus || !storage) return 0;
    struct MsgPort *port=CreateMsgPort();
    struct IOExtTD *request=port?(void *)CreateIORequest(port,sizeof(*request)):NULL;
    UBYTE *buffer=AllocVec(4096,MEMF_PUBLIC|MEMF_CLEAR);
    int opened=0,okay=0;
    if(!request || !buffer) goto done;
    struct IOStdReq *io=&request->iotd_Req;
    if(OpenDevice((CONST_STRPTR)"virtio.device",0,(struct IORequest *)request,0)) goto done;
    opened=1;mark("[VM] full virtio.device unit open PASS\n");
    struct DriveGeometry geometry={0};
    io->io_Command=TD_GETGEOMETRY;io->io_Data=&geometry;io->io_Length=sizeof(geometry);
    LONG error=DoIO((struct IORequest *)request);
    number("[VM] device geometry error",error);
    number("[VM] device sector bytes",geometry.dg_SectorSize);
    number("[VM] device sectors",geometry.dg_TotalSectors);
    if(error || !geometry.dg_TotalSectors ||
        geometry.dg_SectorSize<512 || geometry.dg_SectorSize>4096 ||
        (geometry.dg_SectorSize&(geometry.dg_SectorSize-1))) goto done;
    memset(buffer,0xa5,4096);
    io->io_Command=CMD_READ;io->io_Data=buffer;io->io_Length=geometry.dg_SectorSize;
    io->io_Offset=0;io->io_Actual=0;io->io_Error=0;
    SendIO((struct IORequest *)request);
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t start=ticks();
    while(!CheckIO((struct IORequest *)request) && ticks()-start<frequency*2) Reschedule();
    if(!CheckIO((struct IORequest *)request)) {
        /* Keep request/buffer/port and device alive for this failed boot;
         * the driver may still own them. The outer runner bounds VM lifetime. */
        mark("[VM] full virtio.device read timeout\n");return 0;
    }
    error=WaitIO((struct IORequest *)request);
    number("[VM] device read error",error);number("[VM] device read actual",io->io_Actual);
    number("[VM] device signature",(unsigned)buffer[510]<<8|buffer[511]);
    okay=!error && io->io_Actual==geometry.dg_SectorSize &&
        buffer[510]==0x55 && buffer[511]==0xaa;
done:
    if(opened) CloseDevice((struct IORequest *)request);
    if(request) DeleteIORequest((struct IORequest *)request);
    if(port) DeleteMsgPort(port);
    if(buffer) FreeVec(buffer);
    return okay;
}
