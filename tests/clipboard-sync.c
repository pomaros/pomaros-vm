// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Exercise the original APL guest engine against real Exec devices, without
// copying its implementation into this MIT repository.
#define VM_SYNC_ENGINE_ONLY
#include "sync.c"
static void clip_mark(const char *s) { while(*s)RawPutChar(*s++); }
static const UBYTE textClip[]={
    'F','O','R','M',0,0,0,16,'F','T','X','T',
    'C','H','R','S',0,0,0,4,'A','R','O','S'
};
static const UBYTE imageClip[]={
    'F','O','R','M',0,0,0,56,'I','L','B','M',
    'B','M','H','D',0,0,0,20,
    0,1,0,1,0,0,0,0,1,0,0,0,0,0,1,1,0,1,0,1,
    'C','M','A','P',0,0,0,6,0,0,0,255,0,0,
    'B','O','D','Y',0,0,0,2,128,0
};
static BOOL clipEquals(struct Clipboard *clip,const UBYTE *expected,ULONG size)
{
    ULONG bytes;LONG id;UBYTE *p=readClip(clip,&bytes,&id);
    BOOL okay=p && bytes==size && !memcmp(p,expected,size);
    if(p)FreeVec(p);return okay;
}
static BOOL hostEquals(const UBYTE *expected,ULONG size)
{
    struct VMExchangeClient c;UBYTE request[8],reply[128];ULONG got;BOOL okay=FALSE;
    if(!VMExchangeOpen(&c))return FALSE;
    putLE(request,(UQUAD)-1,8);
    if(!VMExchangeRequest(&c,8,request,8,reply,12,&got) || got!=12 || getLE(reply+8,4)!=size)goto done;
    putLE(request,size,4);
    okay=VMExchangeRequest(&c,9,request,4,reply,sizeof(reply),&got) && got==size && !memcmp(reply,expected,size);
done:VMExchangeClose(&c);return okay;
}
static BOOL hostPublish(const UBYTE *data,ULONG size,UQUAD revision)
{
    struct VMExchangeClient c;UBYTE request[12],reply[8];ULONG got;BOOL okay=FALSE;
    if(!VMExchangeOpen(&c))return FALSE;
    putLE(request,revision,8);putLE(request+8,size,4);
    okay=VMExchangeRequest(&c,10,request,12,NULL,0,&got) && !got &&
        VMExchangeRequest(&c,11,data,size,NULL,0,&got) && !got &&
        VMExchangeRequest(&c,12,NULL,0,reply,8,&got) && got==8;
    VMExchangeClose(&c);return okay;
}
int test_clipboard_sync(void)
{
    BPTR request=Lock((CONST_STRPTR)"SYS:CLIPBOARD.REQ",ACCESS_READ);
    if(!request)return 1;
    UnLock(request);
    BOOL okay=FALSE,assigned=FALSE;struct Clipboard clip={0};
    if(!Inhibit((CONST_STRPTR)"VMTEST0:",DOSFALSE))goto done;
    BPTR lock=CreateDir((CONST_STRPTR)"VMTEST0:CLIPS");
    if(!lock)lock=Lock((CONST_STRPTR)"VMTEST0:CLIPS",ACCESS_READ);
    if(!lock)goto done;
    assigned=AssignLock((CONST_STRPTR)"CLIPS",lock);
    if(!assigned) { UnLock(lock);goto done; }
    /* Check the actual independently loaded command startup/import first. */
    BPTR program=LoadSeg((CONST_STRPTR)"SYS:C/VMSync");
    if(!program)goto done;
    UBYTE arguments[]="ONCE\n";
    LONG result=RunCommand(program,65536,arguments,sizeof(arguments)-1);
    UnLoadSeg(program);
    if(result || !openClip(&clip))goto done;
    ULONG bytes;LONG id;UBYTE *initial=readClip(&clip,&bytes,&id);
    const char *seed="Macaros clipboard qualification";
    ULONG length=strlen(seed),stride=8+length+(length&1);
    BOOL seeded=initial && bytes==12+2*stride && !memcmp(initial+8,"FTXTCHRS",8) &&
        getBE(initial+16)==length && !memcmp(initial+20,seed,length) &&
        !memcmp(initial+12+stride,"UTF8",4) && getBE(initial+16+stride)==length &&
        !memcmp(initial+20+stride,seed,length);
    if(initial)FreeVec(initial);
    if(!seeded)goto done;
    UQUAD revision=(UQUAD)-1;LONG localID=clipID(&clip),written;
    if(!syncOnce(&clip,&revision,&localID))goto done;
    UQUAD stable=revision;LONG stableLocal=localID;
    if(!syncOnce(&clip,&revision,&localID) || revision!=stable || localID!=stableLocal)goto done;
    if(!writeClip(&clip,textClip,sizeof(textClip),&written) ||
       !syncOnce(&clip,&revision,&localID) || !hostEquals(textClip,sizeof(textClip)))goto done;
    if(!hostPublish(imageClip,sizeof(imageClip),revision) ||
       !syncOnce(&clip,&revision,&localID) || !clipEquals(&clip,imageClip,sizeof(imageClip)))goto done;
    UBYTE otherImage[sizeof(imageClip)];memcpy(otherImage,imageClip,sizeof(imageClip));
    otherImage[sizeof(otherImage)-2]=0;
    if(!writeClip(&clip,otherImage,sizeof(otherImage),&written) ||
       !syncOnce(&clip,&revision,&localID) || !hostEquals(otherImage,sizeof(otherImage)))goto done;
    /* Simultaneous copies: a changed host clip wins over the pending guest clip. */
    if(!hostPublish(textClip,sizeof(textClip),revision) ||
       !writeClip(&clip,imageClip,sizeof(imageClip),&written) ||
       !syncOnce(&clip,&revision,&localID) || !clipEquals(&clip,textClip,sizeof(textClip)))goto done;
    stable=revision;stableLocal=localID;
    if(!syncOnce(&clip,&revision,&localID) || revision!=stable || localID!=stableLocal)goto done;
    struct VMExchangeClient first,second;
    if(!VMExchangeOpen(&first))goto done;
    BOOL busy=VMExchangeTryOpen(&second);
    if(busy)VMExchangeClose(&second);
    VMExchangeClose(&first);
    if(busy || !syncOnce(&clip,&revision,&localID))goto done;
    okay=TRUE;
done:
    closeClip(&clip);
    if(assigned && !AssignLock((CONST_STRPTR)"CLIPS",0))okay=FALSE;
    if(!Inhibit((CONST_STRPTR)"VMTEST0:",DOSTRUE))okay=FALSE;
    clip_mark(okay?"[VM] clipboard device text image sync no-echo PASS\n":"[VM] clipboard device text image sync no-echo FAIL\n");
    return okay;
}
