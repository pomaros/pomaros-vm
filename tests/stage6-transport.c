// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <aros/debug.h>
#include <string.h>
#include "client.h"
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
int __nocommandline;
static BOOL hello(struct VMExchangeClient *c)
{
    UBYTE reply[64];ULONG got=0;
    return VMExchangeRequest(c,1,NULL,0,reply,sizeof(reply),&got) &&
      got==sizeof("Macaros host services 1")-1 && !memcmp(reply,"Macaros host services 1",got);
}
static LONG exchange(const char *args)
{
    BPTR seg=LoadSeg((CONST_STRPTR)"SYS:C/VMExchange");
    if(!seg)return 20;
    LONG result=RunCommand(seg,65536,(UBYTE *)args,strlen(args));
    UnLoadSeg(seg);return result;
}
int main(void)
{
    struct VMExchangeClient held;
    BOOL okay=VMExchangeOpen(&held) && hello(&held);
    bug("[STAGE6] initial host session %s\n",okay?"PASS":"FAIL");
    if(!okay)return 20;
    bug("[STAGE6] ready for forced transport cut\n");
    Delay(150);
    BOOL broken=!hello(&held);
    bug("[STAGE6] held session cut %s\n",broken?"PASS":"FAIL");
    VMExchangeClose(&held);
    if(!broken)return 20;
    BOOL reopened=FALSE;
    for(unsigned i=0;i<5&&!reopened;++i) {
        struct VMExchangeClient next;
        if(VMExchangeOpen(&next)) { reopened=hello(&next);VMExchangeClose(&next); }
        if(!reopened)Delay(10);
    }
    bug("[STAGE6] new host session after cut %s\n",reopened?"PASS":"FAIL");
    if(!reopened)return 20;
    const char payload[]="stage6-forced-cut-fixture-20261001\n";
    BPTR source=Open((CONST_STRPTR)"WORK:stage6-cut-source.txt",MODE_NEWFILE);
    if(!source)return 20;
    BOOL written=Write(source,payload,sizeof(payload)-1)==sizeof(payload)-1 && Flush(source);
    written=Close(source)&&written;
    if(!written)return 20;
    LONG put=exchange("PUT WORK:stage6-cut-source.txt stage6-cut-host.txt\n");
    LONG get=put?20:exchange("GET stage6-cut-host.txt WORK:stage6-cut-copy.txt\n");
    bug("[STAGE6] file exchange after cut %s put=%ld get=%ld\n",put==0&&get==0?"PASS":"FAIL",(long)put,(long)get);
    if(put||get)return 20;
    LONG prefs=SystemTags((CONST_STRPTR)"SYS:C/IPrefs.real", TAG_DONE);
    if(prefs)return prefs;
    return SystemTags((CONST_STRPTR)"Run >NIL: SYS:C/Stage6Image", TAG_DONE);
}
