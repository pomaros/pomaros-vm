// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/exec.h>
#include <proto/oop.h>
#include <hidd/pci.h>
/* Compile the existing driver transport and queue bodies unchanged. */
#include "virtio_hw.c"
#include "virtio_queue.c"
static uint64_t ticks(void) { uint64_t n;__asm__ volatile("mrs %0,cntvct_el0":"=r"(n));return n; }
struct BlockIRQ { volatile ULONG hits;volatile UBYTE *isr; };
static AROS_INTH1(block_irq,struct BlockIRQ *,state)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;(void)__ufi_custom;(void)__ufi_code;(void)SysBase;
    if(state->isr) (void)*state->isr;
    ++state->hits;
    return TRUE;
    AROS_INTFUNC_EXIT
}
int test_block_queue(OOP_Object *device,struct Library *oop,int irq_mode)
{
    struct VirtIOBase base={0},*VirtIOBase=&base;
    __typeof__(*(device_t)0) data={0};device_t dev=&data;
    base.virtio_OOPBase=oop;
    base.virtio_HiddPCIDeviceAttrBase=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    if(!base.virtio_HiddPCIDeviceAttrBase) return 0;
    base.virtio_HiddPCIDeviceMethodBase=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,0);
    base.virtio_HiddPCIDriverMethodBase=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDriver,0);
    dev->dev_VirtIOBase=&base;dev->dev_Object=device;
    OOP_GetAttr(device,aHidd_PCIDevice_Driver,(IPTR *)&dev->dev_PCIDriverObject);
    struct MsgPort *port=CreateMsgPort();
    int okay=0,registered=0,stopped=0;
    struct BlockIRQ state={0};
    struct Interrupt interrupt={0};
    union { __typeof__(&block_irq) typed;VOID_FUNC erased; } code={.typed=block_irq};
    interrupt.is_Code=code.erased;interrupt.is_Data=&state;
    interrupt.is_Node.ln_Name=(char *)"Block MSI-X proof";
    if(!port || !virtio_hw_map(dev)) goto done;
    UWORD command=HIDD_PCIDevice_ReadConfigWord(device,4);
    HIDD_PCIDevice_WriteConfigWord(device,4,command|6);
    if(!virtio_hw_reset(dev) || !virtio_hw_negotiate_features(dev,(UQUAD)1<<VIRTIO_F_VERSION_1)) goto stop;
    dev->dev_VQ=virtio_alloc_queue(dev,0,8);
    if(!dev->dev_VQ || !virtio_hw_setup_queue(dev,0,dev->dev_VQ)) goto stop;
    dev->dev_VQ->q_Avail->flags=irq_mode?0:1;
    if(irq_mode) {
        state.isr=dev->dev_ISR;
        registered=HIDD_PCIDriver_AddInterrupt(dev->dev_PCIDriverObject,device,&interrupt);
        if(!registered) goto stop;
        if(!virtio_hw_route_irq(dev)) goto stop;
    }
    if(!virtio_hw_driver_ok(dev)) goto stop;
    UBYTE sector[512];memset(sector,0xa5,sizeof(sector));
    struct IOStdReq io={0};io.io_Message.mn_ReplyPort=port;
    struct virtio_request request={0};
    request.vr_IORequest=(struct IORequest *)&io;
    request.vr_Header.type=VIRTIO_BLK_T_IN;request.vr_Header.sector=0;
    request.vr_DataAddr=sector;request.vr_DataLength=sizeof(sector);
    if(virtio_submit_blk_request(dev->dev_VQ,&request)<0) goto stop;
    uint64_t frequency;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    uint64_t start=ticks();unsigned complete=0;
    do {
        if(!irq_mode || state.hits) complete=virtio_process_used(dev->dev_VQ);
    } while(!complete && ticks()-start<frequency);
    okay=complete==1 && GetMsg(port)==&io.io_Message && !io.io_Error &&
        io.io_Actual==512 && sector[510]==0x55 && sector[511]==0xaa;
stop:
    stopped=virtio_hw_stop(dev);
    if(registered) HIDD_PCIDriver_RemoveInterrupt(dev->dev_PCIDriverObject,device,&interrupt);
    if(!stopped) {
        /* This failed boot retains command code. DMA remains reserved. */
        return 0;
    }
    if(dev->dev_VQ) virtio_free_queue(dev->dev_VQ);
    virtio_hw_unmap(dev);
    HIDD_PCIDevice_WriteConfigWord(device,4,command);
done:
    if(port) DeleteMsgPort(port);
    OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    return okay;
}
