// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __NOLIBBASE__
#define _AROS_TYPES_TIMEVAL_S_H_
#include <proto/exec.h>
#include <aros/macros.h>
#include <devices/usbhardware.h>
#include <hardware/usb/xhci.h>
#define UHWCMD_H
#define XHCIPROTO_H
#define XHCICHIP_SCHEDULE_H
#define USB_DEVEP_CNT 32
#define MAX_DEVENDPOINTS 32
#define XHCI_EVENT_RING_TRBS 8
struct pcisusbXHCIRing { unsigned unused; };
struct pciusbXHCIDevice { struct { APTR dmaa_Ptr; } dc_EPAllocs[32]; ULONG dc_SlotID; };
struct pciusbXHCIIODevPrivate {
 struct pciusbXHCIDevice *dpDevice;
 ULONG dpEPID;
 UWORD dpSTRB,dpTxSTRB,dpTxETRB,dpSttTRB;
};
struct PCIUnit { struct IOUsbHWReq *hu_DevBusyReq[32]; ULONG hu_NakTimeoutFrame[32]; };
struct PCIController { struct PCIUnit *hc_Unit; struct List hc_TDQueue,hc_IntXFerQueue,hc_PeriodicTDQueue; ULONG hc_FrameCounter; };
static unsigned depth,replies,removes,requeues,publishes,doorbells,frees;
static unsigned fast_complete;
static unsigned result_mode; /* 0: capacity rejection, 1: prepared failure, 2: published */
static void disable(void) { ++depth; }
static void enable(void) { assert(depth);--depth; }
static void remove_node(struct Node *n)
{
 assert(n->ln_Pred && n->ln_Succ);++removes;
 n->ln_Pred->ln_Succ=n->ln_Succ;n->ln_Succ->ln_Pred=n->ln_Pred;
 n->ln_Pred=n->ln_Succ=NULL;
}
static void add_head(struct List *l,struct Node *n)
{ assert(!n->ln_Pred && !n->ln_Succ);++requeues;n->ln_Succ=l->lh_Head;n->ln_Pred=(struct Node *)&l->lh_Head;l->lh_Head->ln_Pred=n;l->lh_Head=n; }
static void add_tail(struct List *l,struct Node *n)
{ assert(!n->ln_Pred && !n->ln_Succ);n->ln_Succ=(struct Node *)&l->lh_Tail;n->ln_Pred=l->lh_TailPred;l->lh_TailPred->ln_Succ=n;l->lh_TailPred=n; }
static void init_list(struct List *l)
{ memset(l,0,sizeof(*l));l->lh_Head=(struct Node *)&l->lh_Tail;l->lh_TailPred=(struct Node *)&l->lh_Head; }
static void reply(struct Message *m)
{ assert(!depth && !m->mn_Node.ln_Pred && !m->mn_Node.ln_Succ);++replies; }
static void free_mem(APTR p,ULONG n) { assert(n==sizeof(struct pciusbXHCIIODevPrivate));++frees;free(p); }
#undef Disable
#define Disable() disable()
#undef Enable
#define Enable() enable()
#undef Remove
#define Remove(n) remove_node(n)
#undef AddHead
#define AddHead(l,n) add_head(l,n)
#undef AddTail
#define AddTail(l,n) add_tail(l,n)
#undef ReplyMsg
#define ReplyMsg(m) reply(m)
#undef FreeMem
#define FreeMem(p,n) free_mem(p,n)
#define pciusbXHCIDebug(...) ((void)0)
#define pciusbXHCIDebugV(...) ((void)0)
#define pciusbXHCIDebugTRB(...) ((void)0)
#define pciusbWarn(...) ((void)0)
#define pciusbError(...) ((void)0)
static UWORD xhciEffectiveDataDir(const struct IOUsbHWReq *io) { return io->iouh_Dir; }
static UWORD xhciDevEPKey(const struct IOUsbHWReq *io) { return io->iouh_Endpoint; }
static ULONG xhciBuildDataTRBFlags(struct IOUsbHWReq *io,ULONG type) { (void)io;return type; }
static void xhciFinishRequest(struct PCIController *hc,struct PCIUnit *unit,struct IOUsbHWReq *io)
{ (void)hc;(void)unit;(void)io;assert(!"active completion outside test scope"); }
static BOOL submit(struct PCIController *hc,struct IOUsbHWReq *io)
{
 if(result_mode==1)io->iouh_Req.io_Error=UHIOERR_HOSTERROR;
 if(result_mode==2) {
  ++publishes;++doorbells;
  add_tail(io->iouh_Req.io_Command==UHCMD_INTXFER?&hc->hc_PeriodicTDQueue:&hc->hc_TDQueue,&io->iouh_Req.io_Message.mn_Node);
  if(fast_complete) {
   remove_node(&io->iouh_Req.io_Message.mn_Node);
   hc->hc_Unit->hu_DevBusyReq[io->iouh_Endpoint]=NULL;
   hc->hc_Unit->hu_NakTimeoutFrame[io->iouh_Endpoint]=0;
   free_mem(io->iouh_DriverPrivate1,sizeof(struct pciusbXHCIIODevPrivate));
   ++replies;free(io);
  }
  return TRUE;
 }
 return FALSE;
}
static BOOL xhciQueueControlStages(struct PCIController *hc,struct IOUsbHWReq *io,struct pciusbXHCIIODevPrivate *p,volatile struct pcisusbXHCIRing *r,ULONG f)
{ (void)hc;(void)p;(void)r;(void)f;return submit(hc,io); }
static WORD xhciQueuePayloadTRBs(struct PCIController *hc,struct IOUsbHWReq *io,struct pciusbXHCIIODevPrivate *p,volatile struct pcisusbXHCIRing *r,ULONG f,BOOL last)
{ (void)hc;(void)p;(void)r;(void)f;(void)last;return submit(hc,io)?0:-1; }
#include "xhci_request.c"
#include "xhci_async.c"
#undef base
#include "xhci_periodic.c"
int main(void)
{
 struct PCIUnit unit={0};struct PCIController hc={.hc_Unit=&unit,.hc_FrameCounter=100};
 struct pciusbXHCIDevice device={0};struct pcisusbXHCIRing ring={0};
 struct List pending;struct IOUsbHWReq io={0},other={0};
 device.dc_EPAllocs[1].dmaa_Ptr=&ring;io.iouh_Endpoint=1;
 io.iouh_Dir=UHDIR_IN;io.iouh_Flags=UHFF_NAKTIMEOUT;io.iouh_NakTimeout=10;
 io.iouh_SetupData.bRequest=6;
 for(unsigned kind=0;kind<2;++kind)
 for(unsigned mode=0;mode<5;++mode) {
  struct pciusbXHCIIODevPrivate *priv=calloc(1,sizeof(*priv));assert(priv);
  priv->dpDevice=mode==4?NULL:&device;priv->dpEPID=1;io.iouh_DriverPrivate1=priv;
  io.iouh_Req.io_Error=0;io.iouh_Req.io_Command=kind?UHCMD_BULKXFER:UHCMD_CONTROLXFER;
  init_list(&pending);init_list(&hc.hc_TDQueue);add_tail(&pending,&io.iouh_Req.io_Message.mn_Node);
  replies=removes=requeues=publishes=doorbells=frees=0;
  result_mode=mode;device.dc_EPAllocs[1].dmaa_Ptr=mode==3?NULL:&ring;
  xhciScheduleAsyncTDs(&hc,&pending,io.iouh_Req.io_Command);
  assert(!depth && removes==1);
  if(mode==0) {
   assert(!replies && requeues==1 && !publishes && !doorbells && !frees);
   assert(!unit.hu_DevBusyReq[1] && !unit.hu_NakTimeoutFrame[1]);
   assert(priv->dpSTRB==(UWORD)-1 && priv->dpSttTRB==(UWORD)-1);
   /* Retry the very same IO after capacity becomes available. */
   result_mode=2;
   xhciScheduleAsyncTDs(&hc,&pending,io.iouh_Req.io_Command);
   assert(publishes==1 && doorbells==1 && !replies && removes==2);
  } else if(mode==1 || mode==3 || mode==4) {
   assert(replies==1 && !requeues && !publishes && !doorbells && frees==1);
   assert(!unit.hu_DevBusyReq[1] && !unit.hu_NakTimeoutFrame[1] && !io.iouh_DriverPrivate1);
  } else assert(!replies && !requeues && publishes==1 && doorbells==1);
  if(io.iouh_DriverPrivate1) {
   assert(unit.hu_DevBusyReq[1]==&io && hc.hc_TDQueue.lh_Head==(struct Node *)&io);
   remove_node(&io.iouh_Req.io_Message.mn_Node);free(priv);io.iouh_DriverPrivate1=NULL;
   unit.hu_DevBusyReq[1]=NULL;unit.hu_NakTimeoutFrame[1]=0;
  }
 }
 /* Interrupt scheduling keeps its existing retry policy, but must release
  * the reservation so the next pass can actually attempt that retry. */
 for(unsigned missing=0;missing<2;++missing) {
  struct pciusbXHCIIODevPrivate ip={.dpDevice=&device,.dpEPID=1};
  io.iouh_DriverPrivate1=&ip;io.iouh_Req.io_Error=0;io.iouh_Req.io_Command=UHCMD_INTXFER;
  init_list(&hc.hc_IntXFerQueue);init_list(&hc.hc_PeriodicTDQueue);
  add_tail(&hc.hc_IntXFerQueue,&io.iouh_Req.io_Message.mn_Node);
  replies=removes=requeues=publishes=doorbells=0;result_mode=0;
  device.dc_EPAllocs[1].dmaa_Ptr=missing?NULL:&ring;
  xhciScheduleIntTDs(&hc);
  assert(!depth && removes==1 && requeues==1 && !replies && !publishes);
  assert(!unit.hu_DevBusyReq[1] && !unit.hu_NakTimeoutFrame[1]);
  device.dc_EPAllocs[1].dmaa_Ptr=&ring;result_mode=2;
  xhciScheduleIntTDs(&hc);
  assert(publishes==1 && doorbells==1 && !replies && removes==2);
  assert(hc.hc_PeriodicTDQueue.lh_Head==(struct Node *)&io);
  remove_node(&io.iouh_Req.io_Message.mn_Node);io.iouh_DriverPrivate1=NULL;
  unit.hu_DevBusyReq[1]=NULL;unit.hu_NakTimeoutFrame[1]=0;
 }
 /* A completion task may reap the current IO before the submission helper
  * returns. Exercise the real scheduler success paths with both records freed. */
 for(unsigned kind=0;kind<3;++kind) {
  struct IOUsbHWReq *fast=calloc(1,sizeof(*fast));assert(fast);
  struct pciusbXHCIIODevPrivate *fp=calloc(1,sizeof(*fp));assert(fp);
  fp->dpDevice=&device;fp->dpEPID=1;fast->iouh_DriverPrivate1=fp;
  fast->iouh_Endpoint=1;fast->iouh_SetupData.bRequest=6;
  const UWORD commands[]={UHCMD_CONTROLXFER,UHCMD_BULKXFER,UHCMD_INTXFER};
  fast->iouh_Req.io_Command=commands[kind];
  init_list(&pending);init_list(&hc.hc_IntXFerQueue);
  init_list(&hc.hc_TDQueue);init_list(&hc.hc_PeriodicTDQueue);
  add_tail(kind==2?&hc.hc_IntXFerQueue:&pending,&fast->iouh_Req.io_Message.mn_Node);
  result_mode=2;fast_complete=1;replies=removes=requeues=publishes=doorbells=frees=0;
  if(kind==2)xhciScheduleIntTDs(&hc);
  else xhciScheduleAsyncTDs(&hc,&pending,commands[kind]);
  assert(!depth && replies==1 && publishes==1 && doorbells==1 && frees==1 && removes==2);
  assert(!unit.hu_DevBusyReq[1]);
 }
 fast_complete=0;
 struct pciusbXHCIIODevPrivate priv={.dpDevice=&device,.dpEPID=MAX_DEVENDPOINTS};
 volatile struct pcisusbXHCIRing *out=(APTR)1;
 replies=removes=0;
 assert(!xhciActivateEndpointTransfer(&hc,&unit,&io,&priv,1,&out));
 assert(!out && !replies && !removes && !unit.hu_DevBusyReq[1]);
 unit.hu_DevBusyReq[1]=&other;unit.hu_NakTimeoutFrame[1]=123;
 xhciRollbackUnsubmitted(&unit,&io,&priv,1);
 assert(unit.hu_DevBusyReq[1]==&other && unit.hu_NakTimeoutFrame[1]==123);
 xhciRollbackUnsubmitted(&unit,&io,&priv,USB_DEVEP_CNT);
 assert(!depth);
 puts("xhci-request result=PASS actual-async-and-interrupt-schedulers,capacity-retry,one-reply,prepare-failure,missing-ring,owner-isolation,immediate-completion-free");
}
