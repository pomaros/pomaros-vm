// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define __NOLIBBASE__
#define _AROS_TYPES_TIMEVAL_S_H_
#include <proto/exec.h>
#include <devices/usbhardware.h>
#include <hardware/usb/xhci.h>
#define XHCI_EVENT_RING_TRBS 8
struct PCIController { unsigned unused; };
struct pcisusbXHCIRing { struct IORequest **ringio; };
struct pciusbXHCIDevice { struct { APTR dmaa_Ptr; } dc_EPAllocs[32]; };
struct pciusbXHCIIODevPrivate {
 struct pciusbXHCIDevice *dpDevice; ULONG dpEPID;
 UWORD dpSTRB,dpTxSTRB,dpTxETRB,dpSttTRB;
 APTR dpBounceBuf,dpBounceData;ULONG dpBounceLen;UWORD dpBounceDir;UBYTE dpCC;
 ULONG dpRemaining;BOOL dpCompletionRetired;
};
static BOOL inIRQ;
static unsigned masked;
static void mask(void) { ++masked; }
static void unmask(void) { assert(masked);--masked; }
#undef Disable
#undef Enable
#define Disable() mask()
#define Enable() unmask()
static unsigned releases;static ULONG released_bytes;static APTR released_cookie;
#define pciusbXHCIDebugTRBV(...) ((void)0)
#define pciusbXHCIDebugRIO(...) ((void)0)
#define pciusbWarn(...) ((void)0)
#define xhciDiagDumpEndpointBrief(...) ((void)0)
#define xhciDumpEndpointCtx(...) ((void)0)
static UWORD xhciEffectiveDataDir(const struct IOUsbHWReq *io) { return io->iouh_Dir; }
static BOOL xhciRingioMatchesIOReq(struct pcisusbXHCIRing *r,UWORD idx,APTR io)
{ assert(idx<XHCI_EVENT_RING_TRBS);return r->ringio[idx]==io; }
static void xhciReleaseDMABuffer(struct PCIController *hc,struct IOUsbHWReq *io,ULONG bytes,UWORD dir,APTR cookie)
{ (void)hc;(void)io;(void)dir;assert(!inIRQ && masked);++releases;released_bytes=bytes;released_cookie=cookie; }
#include "xhci_completion.inc"
int main(void)
{
 struct PCIController hc={0};struct IOUsbHWReq io={0},other={0};
 struct IORequest *owners[XHCI_EVENT_RING_TRBS],*savedOwners[XHCI_EVENT_RING_TRBS];
 struct pcisusbXHCIRing ring={.ringio=owners};struct pciusbXHCIDevice dev={0};
 dev.dc_EPAllocs[1].dmaa_Ptr=&ring;
 UBYTE data[64],cookie=0;
 const UWORD commands[]={UHCMD_CONTROLXFER,UHCMD_BULKXFER,UHCMD_INTXFER,UHCMD_ISOXFER};
 const ULONG stops[]={TRB_CC_STOPPED,TRB_CC_STOPPED_LENGTH_INVALID,TRB_CC_STOPPED_SHORT_PACKET};
 const ULONG residuals[]={0,7,UINT32_MAX};
 unsigned cases=0;
 for(unsigned kind=0;kind<4;++kind) {
  memset(&io,0,sizeof(io));io.iouh_Length=sizeof(data);io.iouh_Data=data;
  io.iouh_Req.io_Command=commands[kind];io.iouh_Dir=UHDIR_IN;io.iouh_Actual=9;
  struct pciusbXHCIIODevPrivate priv={.dpDevice=&dev,.dpEPID=1,.dpSTRB=2,.dpSttTRB=3,
   .dpTxSTRB=6,.dpTxETRB=1,.dpBounceBuf=&cookie,.dpBounceData=data,.dpBounceLen=64,.dpBounceDir=UHDIR_IN};
  io.iouh_DriverPrivate1=&priv;
  for(unsigned i=0;i<XHCI_EVENT_RING_TRBS;++i)owners[i]=&io.iouh_Req;
  owners[4]=&other.iouh_Req;owners[5]=NULL;owners[7]=NULL;
  struct IOUsbHWReq savedIO=io;struct pciusbXHCIIODevPrivate savedPriv=priv;
  memcpy(savedOwners,owners,sizeof(owners));releases=0;
  for(unsigned stop=0;stop<3;++stop)
  for(unsigned rem=0;rem<3;++rem) {
   assert(!xhciIntWorkProcess(&hc,&io,residuals[rem],stops[stop]));
   assert(!releases && memcmp(&io,&savedIO,sizeof(io))==0);
   assert(memcmp(&priv,&savedPriv,sizeof(priv))==0 && memcmp(owners,savedOwners,sizeof(owners))==0);
   ++cases;
  }
  /* IRQ observation retains all DMA/owners until the task consumes it. */
  assert(!xhciRetireCompletion(&hc,&io));
  inIRQ=TRUE;
  assert(xhciIntWorkProcess(&hc,&io,13,TRB_CC_SUCCESS));
  assert(!releases && !memcmp(&io,&savedIO,sizeof(io)) && !memcmp(owners,savedOwners,sizeof(owners)));
  assert(priv.dpRemaining==13 && priv.dpCC==TRB_CC_SUCCESS && !priv.dpCompletionRetired);
  savedPriv=priv;
  assert(!xhciIntWorkProcess(&hc,&io,0,TRB_CC_STALL_ERROR));
  assert(!xhciIntWorkProcess(&hc,&io,0,TRB_CC_SUCCESS));
  assert(!xhciIntWorkProcess(&hc,&io,0,TRB_CC_INVALID));
  assert(!memcmp(&priv,&savedPriv,sizeof(priv)) && !releases);
  inIRQ=FALSE;
  assert(xhciRetireCompletion(&hc,&io));
  assert(xhciRetireCompletion(&hc,&io)); /* idempotent task consumption */
  assert(releases==1 && released_bytes==51 && released_cookie==&cookie);
  assert(io.iouh_Actual==51 && priv.dpCC==TRB_CC_SUCCESS && !priv.dpBounceBuf);
  assert(!owners[0] && !owners[1] && !owners[2] && !owners[3] && !owners[6]);
  assert(owners[4]==&other.iouh_Req && !owners[5] && !owners[7]);
  /* Late stopped observations cannot overwrite the completed result. */
  savedIO=io;savedPriv=priv;memcpy(savedOwners,owners,sizeof(owners));
  assert(!xhciIntWorkProcess(&hc,&io,0,TRB_CC_STOPPED));
  assert(releases==1 && memcmp(&io,&savedIO,sizeof(io))==0 && memcmp(&priv,&savedPriv,sizeof(priv))==0);
  assert(memcmp(owners,savedOwners,sizeof(owners))==0);
 }
 unsigned terminalCases=0;
 const ULONG terminalCodes[]={TRB_CC_SUCCESS,TRB_CC_SHORT_PACKET,TRB_CC_STALL_ERROR,TRB_CC_RING_UNDERRUN};
 const ULONG terminalResiduals[]={0,13,64};
 for(unsigned kind=0;kind<4;++kind)for(unsigned out=0;out<2;++out)
 for(unsigned code=0;code<4;++code)for(unsigned rem=0;rem<3;++rem) {
  memset(&io,0,sizeof(io));io.iouh_Length=sizeof(data);io.iouh_Data=data;
  io.iouh_Req.io_Command=commands[kind];io.iouh_Dir=out?UHDIR_OUT:UHDIR_IN;
  struct pciusbXHCIIODevPrivate priv={.dpDevice=&dev,.dpEPID=1,.dpSTRB=(UWORD)-1,.dpSttTRB=(UWORD)-1,
   .dpTxSTRB=1,.dpTxETRB=2,.dpBounceBuf=&cookie,.dpBounceData=data,.dpBounceLen=64,.dpBounceDir=io.iouh_Dir};
  io.iouh_DriverPrivate1=&priv;memset(owners,0,sizeof(owners));owners[1]=owners[2]=&io.iouh_Req;
  releases=0;inIRQ=TRUE;
  assert(xhciIntWorkProcess(&hc,&io,terminalResiduals[rem],terminalCodes[code]));
  assert(!releases && !io.iouh_Actual && owners[1]==&io.iouh_Req && owners[2]==&io.iouh_Req);
  assert(!xhciIntWorkProcess(&hc,&io,0,TRB_CC_SUCCESS));
  inIRQ=FALSE;assert(xhciRetireCompletion(&hc,&io));assert(xhciRetireCompletion(&hc,&io));
  ULONG expected=sizeof(data)-terminalResiduals[rem];
  if(commands[kind]==UHCMD_ISOXFER && out && terminalCodes[code]==TRB_CC_RING_UNDERRUN)expected=0;
  assert(!masked && releases==1 && released_bytes==expected && io.iouh_Actual==expected);
  assert(!owners[1] && !owners[2] && priv.dpCC==terminalCodes[code] && priv.dpCompletionRetired);
  ++terminalCases;
 }
 printf("xhci-task-completion result=PASS terminal-cases=%u control,bulk,int,iso,in,out,success,short,stall,underrun,zero,partial,full\n",terminalCases);
 memset(&io,0,sizeof(io));io.iouh_Length=64;io.iouh_Actual=3;
 struct IOUsbHWReq before=io;releases=0;
 assert(!xhciIntWorkProcess(&hc,&io,UINT32_MAX,TRB_CC_STOPPED_LENGTH_INVALID));
 assert(!releases && memcmp(&io,&before,sizeof(io))==0);
 assert(!xhciIntWorkProcess(&hc,NULL,0,TRB_CC_STOPPED));
 assert(!xhciIntWorkProcess(&hc,NULL,0,TRB_CC_SUCCESS));
 assert(xhciIntWorkProcess(&hc,&io,4,TRB_CC_SUCCESS) && io.iouh_Actual==63);
 printf("xhci-completion result=PASS stop-cases=%u irq-retains-payload-and-owners,task-retires-once,first-event-wins,late-stop,no-private\n",cases);
}
