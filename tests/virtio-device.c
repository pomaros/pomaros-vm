// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <dos/filehandler.h>
#include <dos/dosextens.h>
#include <resources/filesysres.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <string.h>
#include <stdint.h>
#include "virtio_intern.h"
static void mark(const char *p) { while(*p) RawPutChar(*p++); }
static void number(const char *name,uint64_t value)
{
    mark(name);mark("=0x");
    for(int shift=60;shift>=0;shift-=4) RawPutChar("0123456789abcdef"[(value>>shift)&15]);
    mark("\n");
}
static uint64_t ticks(void) { uint64_t t;__asm__ volatile("mrs %0,cntvct_el0":"=r"(t));return t; }
/* -1 means still owned by the driver: keep all resources until VM shutdown. */
static int transfer(struct IOStdReq *io,UWORD command,ULONG offset,void *data,ULONG bytes)
{
    io->io_Command=command;io->io_Offset=offset;io->io_Data=data;
    io->io_Length=bytes;io->io_Actual=0;io->io_Error=0;
    SendIO((struct IORequest *)io);
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t start=ticks();
    while(!CheckIO((struct IORequest *)io) && ticks()-start<frequency*2) Reschedule();
    if(!CheckIO((struct IORequest *)io)) { mark("[VM] device transfer timeout\n");return -1; }
    LONG error=WaitIO((struct IORequest *)io);
    if(error || io->io_Actual!=bytes) {
        number("[VM] transfer command",command);number("[VM] transfer error",error);
        number("[VM] transfer actual",io->io_Actual);return 0;
    }
    return 1;
}
#define TEST_BASE (64UL*1024*1024)
#define TEST_DISK (68UL*1024*1024)
static UBYTE pattern(unsigned i) { return (UBYTE)((i*37+11)^(i>>8)); }
static int storage_test(struct IOStdReq *io,UBYTE *buffer)
{
    static const char magic[]="MACAROS-VM-STORAGE-TEST-V1\n";
    int result=transfer(io,CMD_READ,TEST_BASE,buffer,4096);
    if(result!=1)return result;
    for(unsigned i=0;i<4096;++i)
        if(buffer[i]!=(i<sizeof(magic)-1?(UBYTE)magic[i]:0))return 0;
    /* Diagnostic inspection: prove CMD_UPDATE will submit a real FLUSH. */
    struct virtio_Unit *unit=(void *)io->io_Unit;
    if(!(unit->au_Bus->ab_Dev->dev_Features&((UQUAD)1<<VIRTIO_BLK_F_FLUSH))) {
        mark("[VM] storage FLUSH not negotiated\n");return 0;
    }
    mark("[VM] storage FLUSH negotiated PASS\n");
    result=transfer(io,CMD_READ,TEST_BASE+4096,buffer,4096);
    if(result!=1)return result;
    int zero=1,match=1;
    for(unsigned i=0;i<4096;++i) { zero&=buffer[i]==0;match&=buffer[i]==pattern(i); }
    if(match) { mark("[VM] storage restart read PASS\n");return 1; }
    if(!zero)return 0;
    for(unsigned i=0;i<4096;++i)buffer[i]=pattern(i);
    result=transfer(io,CMD_WRITE,TEST_BASE+4096,buffer,4096);
    if(result!=1)return result;
    result=transfer(io,CMD_UPDATE,0,NULL,0);
    if(result!=1)return result;
    mark("[VM] storage write flush PASS\n");
    memset(buffer,0xa5,4096);
    result=transfer(io,CMD_READ,TEST_BASE+4096,buffer,4096);
    if(result!=1)return result;
    for(unsigned i=0;i<4096;++i)if(buffer[i]!=pattern(i))return 0;
    mark("[VM] storage write readback PASS\n");return 1;
}
static int filesystem_test(struct IOStdReq *io,UBYTE *buffer,int crash)
{
    struct virtio_Unit *unit=(void *)io->io_Unit;
    if(!(unit->au_Bus->ab_Dev->dev_Features&((UQUAD)1<<VIRTIO_BLK_F_FLUSH)))return 0;
    struct Library *ExpansionBase=OpenLibrary((CONST_STRPTR)"expansion.library",0);
    if(!ExpansionBase)return 0;
    IPTR pp[4+DE_BOOTBLOCKS+1]={0};
    pp[0]=(IPTR)"VMTEST0";pp[1]=(IPTR)"virtio.device";
    pp[4+DE_TABLESIZE]=DE_BOOTBLOCKS;pp[4+DE_SIZEBLOCK]=128;
    pp[4+DE_NUMHEADS]=1;pp[4+DE_SECSPERBLOCK]=1;pp[4+DE_BLKSPERTRACK]=1;
    pp[4+DE_LOWCYL]=TEST_BASE/512;pp[4+DE_HIGHCYL]=80*1024*1024/512-1;
    pp[4+DE_NUMBUFFERS]=32;pp[4+DE_BUFMEMTYPE]=MEMF_PUBLIC;
    pp[4+DE_MAXTRANSFER]=0x100000;pp[4+DE_MASK]=~(IPTR)1;
    pp[4+DE_DOSTYPE]=ID_FAT16_DISK;
    struct DeviceNode *node=MakeDosNode(pp);
    CloseLibrary(ExpansionBase);
    if(!node)return 0;
    struct FileSysResource *fs=OpenResource((CONST_STRPTR)"FileSystem.resource");
    if(fs) {
        Forbid();
        struct FileSysEntry *entry=(void *)fs->fsr_FileSysEntries.lh_Head;
        for(;entry->fse_Node.ln_Succ;entry=(void *)entry->fse_Node.ln_Succ)
            if(entry->fse_DosType==ID_FAT16_DISK && (entry->fse_PatchFlags&0x80)) {
                node->dn_SegList=entry->fse_SegList;
                node->dn_GlobalVec=entry->fse_GlobalVec;break;
            }
        Permit();
    }
    if(!node->dn_SegList || !AddDosEntry((void *)node)) { FreeVec(node);return 0; }
    mark("[VM] filesystem mount begin\n");
    BPTR file=Open((CONST_STRPTR)"VMTEST0:SEED.TXT",MODE_OLDFILE);
    static const char seed[]="MACAROS-VM-FILESYSTEM-TEST-V1\n";
    if(!file)return 0;
    LONG bytes=Read(file,buffer,4096);Close(file);
    if(bytes!=sizeof(seed)-1 || memcmp(buffer,seed,sizeof(seed)-1))return 0;
    mark("[VM] filesystem mount PASS\n");
    int existing=0;
    file=Open((CONST_STRPTR)"VMTEST:DATA/KEPT.BIN",MODE_OLDFILE);
    if(file)existing=1;
    else {
        BPTR dir=CreateDir((CONST_STRPTR)"VMTEST:DATA");if(!dir) { number("[VM] mkdir error",IoErr());return 0; }UnLock(dir);
        mark("[VM] filesystem mkdir PASS\n");
        file=Open((CONST_STRPTR)"VMTEST:DATA/PENDING.BIN",MODE_NEWFILE);if(!file)return 0;
        for(unsigned block=0;block<16;++block) {
            for(unsigned i=0;i<4096;++i)buffer[i]=pattern(i+4096*block);
            if(Write(file,buffer,4096)!=4096) { number("[VM] write error",IoErr());Close(file);return 0; }
        }
        mark("[VM] filesystem wrote bytes\n");
        if(!Close(file)) { number("[VM] close error",IoErr());return 0; }
        if(!Rename((CONST_STRPTR)"VMTEST:DATA/PENDING.BIN",(CONST_STRPTR)"VMTEST:DATA/KEPT.BIN")) { number("[VM] rename error",IoErr());return 0; }
        mark("[VM] filesystem rename PASS\n");
        file=Open((CONST_STRPTR)"VMTEST:DATA/KEPT.BIN",MODE_OLDFILE);if(!file)return 0;
    }
    int okay=1;
    for(unsigned block=0;block<16 && okay;++block) {
        if(Read(file,buffer,4096)!=4096) { okay=0;break; }
        for(unsigned i=0;i<4096;++i)if(buffer[i]!=pattern(i+4096*block))okay=0;
    }
    if(okay && Read(file,buffer,1)!=0)okay=0;
    Close(file);if(!okay) { mark("[VM] filesystem content mismatch\n");return 0; }
    mark("[VM] filesystem content PASS\n");
    if(crash) {
        if(!existing)return 0;
        file=Open((CONST_STRPTR)"VMTEST:DATA/UNFINISHED.BIN",MODE_NEWFILE);
        if(!file)return 0;
        /* No Close/Flush/Inhibit on this branch. The host kills the VM while
         * the file is open and writes continue; no exact DMA cut is claimed. */
        for(unsigned pass=0;pass<100000;++pass) {
            if(pass && Seek(file,0,OFFSET_BEGINNING)==-1)return 0;
            for(unsigned block=0;block<16;++block) {
                for(unsigned i=0;i<4096;++i)buffer[i]=pattern(i+block*4096+pass);
                if(Write(file,buffer,4096)!=4096)return 0;
            }
            if(pass==0)mark("[VM] crash write workload active\n");
        }
        mark("[VM] crash workload unexpectedly finished\n");return 0;
    }
    /* Inhibit drains the filesystem cache before the explicit device flush. */
    if(!Inhibit((CONST_STRPTR)"VMTEST0:",DOSTRUE)) { number("[VM] inhibit error",IoErr());return 0; }
    int result=transfer(io,CMD_UPDATE,0,NULL,0);
    if(result!=1)return result;
    mark(existing?"[VM] filesystem restart PASS\n":"[VM] filesystem write flush PASS\n");
    return 1;
}
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
    int result=transfer(io,CMD_READ,0,buffer,geometry.dg_SectorSize);
    if(result<0)return 0;
    okay=result && buffer[510]==0x55 && buffer[511]==0xaa;
    /* Only the harness-created extended image opts in. The FAT volume ends
     * at 64 MiB; exact geometry plus a full header identify the test range. */
    if(okay && (uint64_t)geometry.dg_TotalSectors*geometry.dg_SectorSize==TEST_DISK) {
        result=storage_test(io,buffer);
        if(result<0)return 0;
        okay=result;
    }
    uint64_t disk_bytes=(uint64_t)geometry.dg_TotalSectors*geometry.dg_SectorSize;
    if(okay && (disk_bytes==80UL*1024*1024 || disk_bytes==84UL*1024*1024)) {
        result=filesystem_test(io,buffer,disk_bytes==84UL*1024*1024);
        if(result<0)return 0;
        okay=result;
    }
done:
    if(opened) CloseDevice((struct IORequest *)request);
    if(request) DeleteIORequest((struct IORequest *)request);
    if(port) DeleteMsgPort(port);
    if(buffer) FreeVec(buffer);
    return okay;
}
