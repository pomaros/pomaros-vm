// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
/* Observe actual guest window messages; never inject guest input. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>
struct IntuitionBase *IntuitionBase;
static void mark(const char *s) { while(*s)RawPutChar(*s++); }
static void hex(unsigned v) {
    for(int n=28;n>=0;n-=4)RawPutChar("0123456789abcdef"[(v>>n)&15]);
}
int main(void) {
    Delay(150);
    IntuitionBase=(struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library",39);
    if(!IntuitionBase) { mark("FAIL: input observer library\n");return 20; }
    struct Window *w=OpenWindowTags(NULL,WA_Left,100,WA_Top,100,WA_Width,640,WA_Height,400,
        WA_Title,(IPTR)"VM input qualification",WA_Activate,TRUE,
        WA_RMBTrap,TRUE,WA_ReportMouse,TRUE,
        WA_IDCMP,IDCMP_RAWKEY|IDCMP_MOUSEMOVE|IDCMP_MOUSEBUTTONS,
        TAG_DONE);
    if(!w) { mark("FAIL: input observer window\n");CloseLibrary((struct Library *)IntuitionBase);return 20; }
    mark("[VM] input observer ready\n");
    unsigned seen=0,moves=0;
    for(unsigned tick=0;tick<1250 && (seen!=15 || moves!=3);++tick) {
        struct IntuiMessage *m;
        while((m=(struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG cls=m->Class;UWORD code=m->Code;WORD mx=m->MouseX,my=m->MouseY;
            ReplyMsg((struct Message *)m);
            mark("[VM] IDCMP class=");hex(cls);mark(" code=");hex(code);
            mark(" x=");hex((unsigned)(int)mx);mark(" y=");hex((unsigned)(int)my);mark("\n");
            if(cls==IDCMP_RAWKEY && code==0x20)seen|=1;
            if(cls==IDCMP_RAWKEY && code==0xa0 && (seen&1))seen|=2;
            if(cls==IDCMP_MOUSEBUTTONS && code==0x68)seen|=4;
            if(cls==IDCMP_MOUSEBUTTONS && code==0xe8 && (seen&4))seen|=8;
            if(cls==IDCMP_MOUSEMOVE) {
                if(mx>=198 && mx<=202 && my>=198 && my<=202)moves|=1;
                if(mx>=298 && mx<=302 && my>=248 && my<=252 && (moves&1))moves|=2;
            }
        }
        Delay(1);
    }
    int passed=seen==15 && moves==3;
    mark(passed?"[VM] window keyboard and pointer PASS\n":"FAIL: input observer missing events\n");
    CloseWindow(w);CloseLibrary((struct Library *)IntuitionBase);
    return passed?0:20;
}
