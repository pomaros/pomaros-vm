// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <cybergraphx/cybergraphics.h>
#include <exec/memory.h>
#include <aros/debug.h>
#include <string.h>
struct IntuitionBase *IntuitionBase;
struct Library *CyberGfxBase;
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
int __nocommandline;
int main(void)
{
    Delay(100);
    LONG launched=SystemTags((CONST_STRPTR)"Run >NIL: SYS:Utilities/MultiView SYS:Prefs/Presets/Wallpapers/2007-comp-entries/04-Mithrandir_wallpaperupdated.jpg",TAG_DONE);
    if(launched) {bug("[STAGE6] image launch FAIL status=%ld\n",(long)launched);return 20;}
    Delay(150);
    IntuitionBase=(struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library",39);
    CyberGfxBase=OpenLibrary((CONST_STRPTR)"cybergraphics.library",0);
    if(!IntuitionBase||!CyberGfxBase) {bug("[STAGE6] image libraries FAIL\n");return 20;}
    struct Screen *screen=LockPubScreen(NULL);
    if(!screen) {bug("[STAGE6] image screen FAIL\n");return 20;}
    LONG x=0,y=0,w=0,h=0;
    ULONG guard=LockIBase(0);
    for(struct Window *window=screen->FirstWindow;window;window=window->NextWindow) {
      if(window->Title && strstr((char *)window->Title,"wallpaperupdated")) {
        x=window->LeftEdge;y=window->TopEdge;w=window->Width;h=window->Height;break;
      }
    }
    UnlockIBase(guard);
    if(w<300||h<250||x<0||y<0||x+200>screen->Width||y+200>screen->Height) {
      bug("[STAGE6] MultiView JPEG window FAIL x=%ld y=%ld w=%ld h=%ld\n",(long)x,(long)y,(long)w,(long)h);
      UnlockPubScreen(NULL,screen);return 20;
    }
    UBYTE *pixels=AllocVec(128*128*3,MEMF_ANY);
    if(!pixels){UnlockPubScreen(NULL,screen);return 20;}
    LONG read=ReadPixelArray(pixels,0,0,128*3,&screen->RastPort,x+60,y+70,128,128,RECTFMT_RGB);
    ULONG different=0;
    for(ULONG i=0;i<128*128;++i)
      if(pixels[3*i]!=pixels[0]||pixels[3*i+1]!=pixels[1]||pixels[3*i+2]!=pixels[2])++different;
    UnlockPubScreen(NULL,screen);
    FreeVec(pixels);
    BPTR proof=Open((CONST_STRPTR)"WORK:stage6-image.txt",MODE_NEWFILE);
    if(proof){FPrintf(proof,(CONST_STRPTR)"launch=%ld x=%ld y=%ld w=%ld h=%ld read=%ld varied=%lu\n",launched,x,y,w,h,read,different);Close(proof);}
    BOOL okay=read>0&&different>4096;
    bug("[STAGE6] MultiView JPEG window and nonflat pixels %s varied=%lu\n",okay?"PASS":"FAIL",(unsigned long)different);
    CloseLibrary(CyberGfxBase);CloseLibrary((struct Library *)IntuitionBase);
    return okay?0:20;
}
