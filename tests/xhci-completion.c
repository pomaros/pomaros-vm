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
 APTR dpBounceBuf,dpBounceData;ULONG dpBounceLen;UWORD dpBounceDir;BYTE dpCC;
};
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
{ (void)hc;(void)io;(void)dir;++releases;released_bytes=bytes;released_cookie=cookie; }
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
  /* A genuine later terminal event must still release exactly once. */
  assert(xhciIntWorkProcess(&hc,&io,13,TRB_CC_SUCCESS));
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
 memset(&io,0,sizeof(io));io.iouh_Length=64;io.iouh_Actual=3;
 struct IOUsbHWReq before=io;releases=0;
 assert(!xhciIntWorkProcess(&hc,&io,UINT32_MAX,TRB_CC_STOPPED_LENGTH_INVALID));
 assert(!releases && memcmp(&io,&before,sizeof(io))==0);
 assert(!xhciIntWorkProcess(&hc,NULL,0,TRB_CC_STOPPED));
 assert(!xhciIntWorkProcess(&hc,NULL,0,TRB_CC_SUCCESS));
 assert(xhciIntWorkProcess(&hc,&io,4,TRB_CC_SUCCESS) && io.iouh_Actual==63);
 printf("xhci-completion result=PASS stop-cases=%u preserve-payload,correlation,cc,length,later-completion,late-stop,no-private\n",cases);
}
