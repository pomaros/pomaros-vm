/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Bounded early-reader evidence. No OS services, allocator or board addresses.
 */
#include "early_fdt.h"
#include "gic.h"
static struct AROSGIC gic;
static volatile unsigned ticks,ipis,unexpected;
static uint32_t timer_id;
static uint64_t interval;
extern char probe_vectors[];
extern int probe_wait(volatile unsigned *ticks);
static void interrupt_probe(const struct AROSEarlyFDT *tree);
static uintptr_t uart;
static void put(char c)
{
    volatile uint32_t *flags=(volatile uint32_t *)(uart+0x18);
    unsigned n=1000000;
    while((*flags&(1U<<5)) && --n) {}
    if(n) *(volatile uint32_t *)uart=(unsigned char)c;
}
static void text(const char *s) {while(*s) put(*s++);}
static void hex(uint64_t value)
{
    static const char digits[]="0123456789abcdef";
    text("0x");for(int n=60;n>=0;n-=4) put(digits[(value>>n)&15]);
}
static void decimal(uint32_t value)
{
    char b[16];unsigned n=0;
    do {b[n++]=(char)('0'+value%10);value/=10;} while(value);
    while(n) put(b[--n]);
}
static int equal(const char *a,int n,const char *b)
{
    unsigned i=0;while(b[i]) {if(i>=(unsigned)n || a[i]!=b[i]) return 0;++i;}
    return i+1==(unsigned)n && !a[i];
}
static uint32_t be32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
void probe(void *blob)
{
    struct AROSEarlyFDT tree;
    /* The entry contract provides a readable FDT header; the trusted firmware
     * owns this RAM. All further accesses are checked against totalsize. */
    if(!blob || !AROSEarlyFDTOpen(&tree,blob,be32((const uint8_t *)blob+4))) return;
    int console=AROSEarlyFDTStdout(&tree);
    if(console<0 || !AROSEarlyFDTCompatible(&tree,console,"arm,pl011")) return;
    uint64_t address,size;
    if(!AROSEarlyFDTReg(&tree,console,0,&address,&size) || size<0x1c) return;
    uart=(uintptr_t)address;
    text("[DT0] tree=");hex((uintptr_t)blob);text(" bytes=");decimal(tree.bytes);text("\n");
    text("[DT0] console=");hex(address);text("\n");
    uint64_t el;__asm__ volatile("mrs %0, CurrentEL":"=r"(el));
    text("[DT0] EL=");decimal((uint32_t)(el>>2));text("\n");
    unsigned cpus=0,memory=0;int node=-1,depth=-1;
    while((node=AROSEarlyFDTNext(&tree,node,&depth))>=0 && depth>=0) {
        int n;const char *kind=AROSEarlyFDTProperty(&tree,node,"device_type",&n);
        if(!kind) continue;
        if(equal(kind,n,"cpu")) {
            if(!AROSEarlyFDTRawReg(&tree,node,0,&address,&size)) {text("[DT0] FAIL cpu-reg\n");return;}
            ++cpus;text("[DT0] cpu=");hex(address);
            const char *enable=AROSEarlyFDTProperty(&tree,node,"enable-method",&n);
            text(" enable=");
            if(enable && n>0 && !enable[n-1]) text(enable);else text("none");
            text("\n");
        } else if(equal(kind,n,"memory")) {
            for(unsigned i=0;AROSEarlyFDTRawReg(&tree,node,i,&address,&size);++i) {
                ++memory;text("[DT0] memory=");hex(address);text("+");hex(size);text("\n");
            }
        }
    }
    char path[AROS_FDT_MAX_PATH];
    if(!AROSEarlyFDTPath(&tree,console,path,sizeof(path))) {text("[DT0] FAIL path\n");return;}
    int n;const char *root=AROSEarlyFDTProperty(&tree,0,"compatible",&n);
    text("[DT0] PASS reader platform=");
    if(AROSEarlyFDTCompatible(&tree,0,"linux,dummy-virt")) text("virt");
    else if(AROSEarlyFDTCompatible(&tree,0,"raspberrypi,4-model-b")) text("rpi4");
    else text("unknown");
    text(" nodes=");decimal(tree.nodes);text(" memory=");decimal(memory);
    text(" cpus=");decimal(cpus);text(" stdout=");text(path);
    text(" root=");if(root && n>0 && !root[n-1]) text(root);else text("missing");text("\n");
    uint8_t saved=*(uint8_t *)blob;*(uint8_t *)blob^=1;
    struct AROSEarlyFDT bad;
    int refused=!AROSEarlyFDTOpen(&bad,blob,tree.bytes);
    *(uint8_t *)blob=saved;
    text(refused?"[DT0] (control) bad-magic refused=1\n":"[DT0] FAIL bad-magic accepted\n");
    if(refused) interrupt_probe(&tree);
}

void probe_exception(void)
{
    uint64_t esr,elr,far;
    __asm__ volatile("mrs %0, esr_el1\n\tmrs %1, elr_el1\n\tmrs %2, far_el1":"=r"(esr),"=r"(elr),"=r"(far));
    text("[DT2] FAIL exception esr=");hex(esr);text(" elr=");hex(elr);text(" far=");hex(far);text("\n");
}
void probe_irq(void)
{
    /* Force the asynchronous handler to exercise x18 restoration. */
    __asm__ volatile("mov x18, #0xbad" ::: "x18");
    uint32_t token=AROSGICAcknowledge(&gic),id=gic.version==3?token:token&1023;
    if(id>=1020) return;
    if(id==timer_id) {
        ++ticks;
        if(ticks>=128) __asm__ volatile("msr cntv_ctl_el0, xzr" ::: "memory");
        else __asm__ volatile("msr cntv_tval_el0, %0"::"r"(interval):"memory");
    } else if(id==1) ++ipis;
    else {++unexpected;(void)AROSGICDisable(&gic,id);}
    AROSGICEnd(&gic,token);
}
static void interrupt_probe(const struct AROSEarlyFDT *tree)
{
    int depth=-1,node=-1,timer=-1;uint32_t trigger;
    struct AROSFDTIRQ irq;
    __asm__ volatile("msr vbar_el1, %0\n\tisb"::"r"(probe_vectors):"memory");
    while((node=AROSEarlyFDTNext(tree,node,&depth))>=0 && depth>=0) {
        if(AROSEarlyFDTCompatible(tree,node,"arm,armv8-timer") && AROSEarlyFDTEnabled(tree,node)) {
            if(timer>=0) {text("[DT2] FAIL duplicate-timer\n");return;}
            timer=node;
        }
    }
    /* Virtual timer (entry 2) is available to a guest as well as bare metal.
     * QEMU/HVF leaves CNTP_TVAL undefined, so a physical-timer-only probe
     * would test a privilege it has not been granted. */
    if(timer<0 || !AROSEarlyFDTInterrupt(tree,timer,2,&irq) ||
       !AROSGICResolve(&irq,&timer_id,&trigger) || timer_id<16 || timer_id>31 ||
       !AROSGICInit(&gic,tree,irq.controller)) {text("[DT2] FAIL binding\n");return;}
    uint64_t frequency,start,end;
    __asm__ volatile("mrs %0, cntfrq_el0\n\tmrs %1, cntvct_el0":"=r"(frequency),"=r"(start));
    interval=frequency/1000;
    if(!interval || interval>0x7fffffff || !AROSGICEnable(&gic,timer_id,trigger) ||
       !AROSGICSendSelfIPI(&gic,1)) {text("[DT2] FAIL enable\n");return;}
    text("[DT2] armed gic=");decimal(gic.version);text(" timer-irq=");decimal(timer_id);text("\n");
    __asm__ volatile("msr cntv_tval_el0, %0\n\tmov x9, #1\n\tmsr cntv_ctl_el0, x9\n\tisb"::"r"(interval):"x9","memory");
    int preserved=probe_wait(&ticks);
    __asm__ volatile("msr daifset, #2\n\tmrs %0, cntvct_el0":"=r"(end)::"memory");
    int stopped=AROSGICDisable(&gic,timer_id) && AROSGICDisable(&gic,1);
    if(!preserved) {text("[DT2] FAIL x18 preservation\n");return;}
    if(!stopped || ipis!=1 || unexpected || end-start>frequency*2) {text("[DT2] FAIL delivery\n");return;}
    text("[DT2] PASS core-bindings gic=");decimal(gic.version);text(" timer-irq=");decimal(timer_id);
    text(" ticks=");decimal(ticks);text(" self-ipi=");decimal(ipis);text(" idle=wfi timer=virtual x18=preserved\n");
}
