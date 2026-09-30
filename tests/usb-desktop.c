// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
/* Start existing Poseidon classes from the desktop's normal DOS process. */
#include <proto/exec.h>
#include <proto/poseidon.h>
#include <utility/tagitem.h>
struct Library *ps;
static void mark(const char *s) { while(*s)RawPutChar(*s++); }
int main(void)
{
    ps=OpenLibrary((CONST_STRPTR)"poseidon.library",5);
    if(!ps) { mark("FAIL: desktop Poseidon library\n");return 20; }
    APTR hub=psdAddClass((STRPTR)"SYS:Classes/USB/hub.class",0);
    APTR hubss=psdAddClass((STRPTR)"SYS:Classes/USB/hubss.class",0);
    APTR hid=psdAddClass((STRPTR)"SYS:Classes/USB/hid.class",0);
    if(!hub || !hubss || !hid) { mark("FAIL: desktop USB classes\n");return 20; }
    mark("[VM] desktop USB classes loaded\n");
    APTR hw=psdAddHardware((STRPTR)"DEVS:USBHardware/pcixhci.device",0);
    if(!hw || !psdEnumerateHardware(hw)) { mark("FAIL: desktop USB hardware\n");return 20; }
    psdClassScan();
    for(unsigned poll=0;poll<100;++poll) {
        unsigned devices=0;
        psdLockReadPBase();
        APTR pd=NULL;
        while((pd=psdFindDeviceA(pd,NULL))) {
            APTR hubdev=NULL;IPTR connected=0,configured=0;
            struct TagItem tags[]={{DA_HubDevice,(IPTR)&hubdev},{DA_IsConnected,(IPTR)&connected},
                {DA_IsConfigured,(IPTR)&configured},{TAG_DONE,0}};
            psdGetAttrsA(PGA_DEVICE,pd,tags);
            if(!hubdev || !connected || !configured)continue;
            APTR interface=NULL;
            while((interface=psdFindInterfaceA(pd,interface,NULL))) {
                APTR binding=NULL,cls=NULL;
                struct TagItem itags[]={{IFA_Binding,(IPTR)&binding},{IFA_BindingClass,(IPTR)&cls},{TAG_DONE,0}};
                psdGetAttrsA(PGA_INTERFACE,interface,itags);
                if(binding && cls==hid) { ++devices;break; }
            }
        }
        psdUnlockPBase();
        if(devices>=2) { mark("[VM] desktop two HID bindings PASS\n");return 0; }
        psdDelayMS(100);
    }
    mark("FAIL: desktop HID binding timeout\n");
    return 20;
    /* Poseidon owns the live classes/tasks. Keep the library open until VM exit. */
}
