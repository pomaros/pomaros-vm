// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
/* Exercise the volume mounted by normal desktop startup, never mount it here. */
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include "virtio_intern.h"
#include <aros/debug.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <devices/trackdisk.h>
#include <string.h>
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
static void mark(const char *s) { while(*s) RawPutChar(*s++); }
static unsigned char expected[4096], actual[4096];
int main(void)
{
    const CONST_STRPTR path=(CONST_STRPTR)"WORK:VM-WORK-QUALIFY.bin";
    const char *stage="open-existing";
    BPTR f=Open(path,MODE_OLDFILE);
    int reading=f!=BNULL, okay=1;
    if(!reading) {
        if(IoErr()!=ERROR_OBJECT_NOT_FOUND) goto fail;
        stage="create";
        f=Open(path,MODE_NEWFILE);
        if(!f) goto fail;
    }
    stage="file-transfer";
    for(unsigned block=0;block<16 && okay;++block) {
        for(unsigned i=0;i<sizeof(expected);++i) {
            unsigned offset=block*sizeof(expected)+i;
            expected[i]=(unsigned char)((offset*37+11)^(offset>>8));
        }
        if(reading) okay=Read(f,actual,sizeof(actual))==sizeof(actual) &&
            !memcmp(actual,expected,sizeof(actual));
        else okay=Write(f,expected,sizeof(expected))==sizeof(expected);
    }
    if(reading && okay) okay=Read(f,actual,1)==0;
    if(!reading && okay) okay=Flush(f)!=0;
    if(!Close(f)) okay=0;
    if(!okay) goto fail;
    /* Drain the FAT cache, then request a real device persistence boundary. */
    stage="device-open";
    struct MsgPort *port=CreateMsgPort();
    struct IOExtTD *io=port?(struct IOExtTD *)CreateIORequest(port,sizeof(*io)):NULL;
    int opened=io && !OpenDevice((CONST_STRPTR)"virtio.device",0,(struct IORequest *)io,0);
    int inhibited=0;
    okay=opened;
    if(opened) {
        stage="flush-feature";
        struct virtio_Unit *unit=(void *)io->iotd_Req.io_Unit;
        okay=(unit->au_Bus->ab_Dev->dev_Features&((UQUAD)1<<VIRTIO_BLK_F_FLUSH))!=0;
    }
    if(okay) { stage="inhibit"; okay=inhibited=Inhibit((CONST_STRPTR)"VMWORK0:",DOSTRUE)!=0; }
    if(okay) {
        stage="device-flush";
        io->iotd_Req.io_Command=CMD_UPDATE;
        okay=DoIO((struct IORequest *)io)==0 && io->iotd_Req.io_Error==0;
    }
    if(inhibited && !Inhibit((CONST_STRPTR)"VMWORK0:",DOSFALSE)) { stage="remount"; okay=0; }
    if(opened) CloseDevice((struct IORequest *)io);
    if(io) DeleteIORequest((struct IORequest *)io);
    if(port) DeleteMsgPort(port);
    if(!okay) goto fail;
    mark(reading?"[VM] desktop WORK restart-read PASS\n":"[VM] desktop WORK write-flush PASS\n");
    return 0;
fail:
    bug("[VM] WORK failure stage=%s dos-error=%ld\n",stage,(long)IoErr());
    mark("FAIL: desktop WORK qualification\n");
    return 20;
}
