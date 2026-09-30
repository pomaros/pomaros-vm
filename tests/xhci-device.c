// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/usbhardware.h>
#include <utility/tagitem.h>
#include <proto/poseidon.h>
#include <proto/kernel.h>
extern APTR KernelBase;
#include <stdint.h>
struct Library *ps;
static void mark(const char *s) { while(*s)RawPutChar(*s++); }
static void number(const char *s,ULONG n)
{
    mark(s);mark("=0x");
    for(int shift=28;shift>=0;shift-=4)RawPutChar("0123456789abcdef"[(n>>shift)&15]);
    mark("\n");
}
static void dump_stack_log(void)
{
    struct List *messages=NULL;
    struct TagItem listTags[]={{PA_ErrorMsgList,(IPTR)&messages},{TAG_DONE,0}};
    psdLockReadPBase();
    psdGetAttrsA(PGA_STACK,NULL,listTags);
    unsigned count=0;
    if(messages)for(struct Node *n=messages->lh_Head;n->ln_Succ && count<64;n=n->ln_Succ,++count) {
        STRPTR text=NULL;struct TagItem tags[]={{EMA_Msg,(IPTR)&text},{TAG_DONE,0}};
        psdGetAttrsA(PGA_ERRORMSG,n,tags);
        mark("[VM] Poseidon log: ");
        if(text)for(unsigned i=0;text[i] && i<256;++i)RawPutChar(text[i]);
        mark("\n");
    }
    psdUnlockPBase();
}
static int enumerate_usb(void)
{
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    if(!KernelBase) { mark("FAIL: USB kernel resource\n");return 0; }
    number("[VM] USB entry supervisor",KrnIsSuper());
    mark("[VM] Poseidon hardware attach begin\n");
    if(!psdAddClass((STRPTR)"SYS:Classes/USB/hub.class",0) || !psdAddClass((STRPTR)"SYS:Classes/USB/hubss.class",0)) {
        dump_stack_log();mark("FAIL: Poseidon hub classes\n");return 0;
    }
    APTR hw=psdAddHardware((STRPTR)"DEVS:USBHardware/pcixhci.device",0);
    if(!hw) { dump_stack_log();mark("FAIL: Poseidon hardware attach\n");return 0; }
    mark("[VM] Poseidon hardware attach PASS\n");
    if(!psdEnumerateHardware(hw)) { dump_stack_log();mark("FAIL: Poseidon root enumeration\n");return 0; }
    mark("[VM] Poseidon root enumeration PASS\n");
    mark("[VM] Poseidon class scan begin\n");
    psdClassScan();
    mark("[VM] Poseidon class scan returned\n");
    number("[VM] USB after scan supervisor",KrnIsSuper());
    uint64_t start,now,freq;
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(freq));
    __asm__ volatile("mrs %0,cntvct_el0":"=r"(start));
    for(;;) {
        unsigned children=0;
        psdLockReadPBase();
        APTR pd=NULL;
        while((pd=psdFindDeviceA(pd,NULL))) {
            APTR hub=NULL;IPTR descriptor=0,connected=0,vendor=0,product=0;
            struct TagItem tags[]={{DA_HubDevice,(IPTR)&hub},{DA_HasDevDesc,(IPTR)&descriptor},
                {DA_IsConnected,(IPTR)&connected},{DA_VendorID,(IPTR)&vendor},{DA_ProductID,(IPTR)&product},{TAG_DONE,0}};
            psdGetAttrsA(PGA_DEVICE,pd,tags);
            if(hub && descriptor && connected) {
                ++children;number("[VM] USB child vendor",vendor);number("[VM] USB child product",product);
            }
        }
        psdUnlockPBase();
        if(children>=2) { mark("[VM] xHCI two child descriptors PASS\n");return 1; }
        __asm__ volatile("mrs %0,cntvct_el0":"=r"(now));
        if(now-start>freq*8)break;
        psdDelayMS(100);
    }
    dump_stack_log();mark("FAIL: xHCI child descriptor timeout\n");return 0;
}
/* Isolated VM startup gate. Retain the open unit and heap-backed IO/port until
 * VM termination: this probe does not claim controller teardown qualification. */
int test_xhci_device(void)
{
    BPTR requested=Lock((CONST_STRPTR)"SYS:XHCITEST.REQ",ACCESS_READ);
    if(!requested)return -1;
    UnLock(requested);
    mark("[VM] xHCI driver open begin\n");
    ps=OpenLibrary((CONST_STRPTR)"poseidon.library",5);
    if(!ps) {
        mark("FAIL: xHCI poseidon library\n");return 0;
    }
    mark("[VM] xHCI poseidon library ready\n");
    BPTR enumeration=Lock((CONST_STRPTR)"SYS:XHCIENUM.REQ",ACCESS_READ);
    if(enumeration) { UnLock(enumeration);return enumerate_usb(); }
    struct MsgPort *port=CreateMsgPort();
    struct IOUsbHWReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    if(!io) { mark("FAIL: xHCI probe allocation\n");return 0; }
    LONG error=OpenDevice((CONST_STRPTR)"DEVS:USBHardware/pcixhci.device",0,&io->iouh_Req,0);
    if(error) { number("[VM] xHCI open error",error);mark("FAIL: xHCI driver open\n");return 0; }
    mark("[VM] xHCI driver unit open PASS\n");
    ULONG caps=0,version=0;
    struct TagItem tags[]={{UHA_Capabilities,(IPTR)&caps},{UHA_DriverVersion,(IPTR)&version},{TAG_DONE,0}};
    io->iouh_Req.io_Command=UHCMD_QUERYDEVICE;io->iouh_Data=tags;io->iouh_Length=sizeof(tags);
    error=DoIO(&io->iouh_Req);
    number("[VM] xHCI query capabilities",caps);number("[VM] xHCI query version",version);
    if(error || io->iouh_Actual!=2 || !(caps&UHCF_USB30) || version!=0x300) {
        mark("FAIL: xHCI driver query\n");return 0;
    }
    /* The query completed synchronously; no stack data remains in flight. */
    io->iouh_Data=NULL;io->iouh_Length=0;
    mark("[VM] xHCI driver query PASS\n");return 1;
}
