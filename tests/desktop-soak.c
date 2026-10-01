// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
/* Bounded workload against the normal desktop WORK mount and file commands. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <aros/debug.h>
#include <string.h>
#include <stdio.h>
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
static UBYTE data[4096];
static UBYTE pattern(unsigned cycle,unsigned offset)
{ return (UBYTE)((offset*37+cycle*17)^(offset>>8)); }
static int exchange(const char *verb,unsigned cycle,const char *local)
{
    char args[128];
    if(!strcmp(verb,"PUT"))snprintf(args,sizeof(args),"PUT %s soak-%02u.bin\n",local,cycle);
    else snprintf(args,sizeof(args),"GET soak-%02u.bin %s\n",cycle,local);
    BPTR segment=LoadSeg((CONST_STRPTR)"SYS:C/VMExchange");
    if(!segment)return 0;
    LONG result=RunCommand(segment,65536,(UBYTE *)args,strlen(args));
    UnLoadSeg(segment);return result==0;
}
static int verify(unsigned cycle,const char *name)
{
    BPTR f=Open((CONST_STRPTR)name,MODE_OLDFILE);int okay=f!=0;
    for(unsigned offset=0;okay && offset<65536;offset+=sizeof(data)) {
        if(Read(f,data,sizeof(data))!=sizeof(data)) { okay=0;break; }
        for(unsigned i=0;i<sizeof(data);++i)
            if(data[i]!=pattern(cycle,offset+i)) { okay=0;break; }
    }
    if(okay && Read(f,data,1)!=0)okay=0;
    if(f && !Close(f))okay=0;
    return okay;
}
int main(void)
{
    const char *stage="start";unsigned cycle=0;
    Forbid();
    bug("[VM] desktop soak begin free=%lu largest=%lu\n",
        (unsigned long)AvailMem(MEMF_PUBLIC),(unsigned long)AvailMem(MEMF_PUBLIC|MEMF_LARGEST));
    Permit();
    for(;cycle<60;++cycle) {
        stage="write";
        BPTR f=Open((CONST_STRPTR)"WORK:SOAK-TEMP.bin",MODE_NEWFILE);
        if(!f)goto fail;
        int okay=1;
        for(unsigned offset=0;okay && offset<65536;offset+=sizeof(data)) {
            for(unsigned i=0;i<sizeof(data);++i)data[i]=pattern(cycle,offset+i);
            okay=Write(f,data,sizeof(data))==sizeof(data);
        }
        if(okay)okay=Flush(f)!=0;
        if(!Close(f))okay=0;
        if(!okay)goto fail;
        stage="rename";
        if(cycle && !DeleteFile((CONST_STRPTR)"WORK:SOAK.bin"))goto fail;
        if(!Rename((CONST_STRPTR)"WORK:SOAK-TEMP.bin",(CONST_STRPTR)"WORK:SOAK.bin"))goto fail;
        stage="put";
        if(!exchange("PUT",cycle,"WORK:SOAK.bin"))goto fail;
        stage="get";
        if(cycle && !DeleteFile((CONST_STRPTR)"WORK:SOAK-RETURN.bin"))goto fail;
        if(!exchange("GET",cycle,"WORK:SOAK-RETURN.bin"))goto fail;
        stage="verify";
        if(!verify(cycle,"WORK:SOAK.bin") || !verify(cycle,"WORK:SOAK-RETURN.bin"))goto fail;
        Forbid();
        bug("[VM] desktop soak cycle=%u PASS\n",cycle);
        Permit();
        Delay(50);
    }
    Forbid();
    bug("[VM] desktop soak complete PASS free=%lu largest=%lu\n",
        (unsigned long)AvailMem(MEMF_PUBLIC),(unsigned long)AvailMem(MEMF_PUBLIC|MEMF_LARGEST));
    Permit();
    return 0;
fail:
    Forbid();
    bug("FAIL: desktop soak cycle=%u stage=%s dos=%ld\n",cycle,stage,(long)IoErr());
    Permit();
    return 20;
}
