// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include "early_fdt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void exercise(struct AROSEarlyFDT *t)
{
    uint64_t a,s;struct AROSFDTIRQ irq;
    int uart=AROSEarlyFDTStdout(t);
    assert(uart==AROSEarlyFDTFindPath(t,"/soc/bus/serial@100"));
    assert(AROSEarlyFDTReg(t,uart,0,&a,&s) && a==0x20001100 && s==0x100);
    assert(AROSEarlyFDTInterrupt(t,uart,0,&irq) && irq.count==3 && irq.cells[1]==1);
    int timer=AROSEarlyFDTFindPath(t,"/timer");
    assert(AROSEarlyFDTInterrupt(t,timer,1,&irq) && irq.cells[0]==1 && irq.cells[1]==14);
    assert(!AROSEarlyFDTInterrupt(t,timer,4,&irq));
    int ext=AROSEarlyFDTFindPath(t,"/extended");
    assert(AROSEarlyFDTInterrupt(t,ext,1,&irq) && irq.cells[1]==14);
    assert(!AROSEarlyFDTReg(t,AROSEarlyFDTFindPath(t,"/soc/bus/outside@ff0"),0,&a,&s));
    assert(!AROSEarlyFDTReg(t,AROSEarlyFDTFindPath(t,"/soc/missing/leaf@1"),0,&a,&s));
    assert(!AROSEarlyFDTEnabled(t,AROSEarlyFDTFindPath(t,"/soc/disabled/leaf")));
    assert(AROSEarlyFDTRawReg(t,AROSEarlyFDTFindPath(t,"/cpus/cpu@100"),0,&a,&s) && a==0x100 && s==0);
    assert(AROSEarlyFDTReg(t,AROSEarlyFDTFindPath(t,"/memory@40000000"),1,&a,&s) && a==0x100000000 && s==0x2000000);
}
int main(int argc,char **argv)
{
    assert(argc==2 || argc==3);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>0 && n<=AROS_FDT_MAX_BYTES);rewind(f);
    unsigned char *data=malloc((size_t)n);assert(data);
    assert(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);
    struct AROSEarlyFDT t;assert(AROSEarlyFDTOpen(&t,data,(size_t)n));
    if(argc==3) exercise(&t);
    int depth=-1,node=-1;unsigned nodes=0;
    while((node=AROSEarlyFDTNext(&t,node,&depth))>=0 && depth>=0) {
        char path[AROS_FDT_MAX_PATH];assert(AROSEarlyFDTPath(&t,node,path,sizeof(path)));
        printf("NODE\t%s\n",path);++nodes;
        uint64_t a,s;
        for(unsigned i=0;AROSEarlyFDTRawReg(&t,node,i,&a,&s);++i) {
            printf("REG\t%s\t%u\t%llx\t%llx\n",path,i,(unsigned long long)a,(unsigned long long)s);
        }
        int len;const char *p=AROSEarlyFDTProperty(&t,node,"enable-method",&len);
        if(p && len>0 && p[len-1]==0) printf("ENABLE\t%s\t%s\n",path,p);
    }
    assert(nodes==t.nodes);
    int stdout_node=AROSEarlyFDTStdout(&t);
    if(stdout_node>=0) {
        char path[AROS_FDT_MAX_PATH];assert(AROSEarlyFDTPath(&t,stdout_node,path,sizeof(path)));
        printf("STDOUT\t%s\n",path);
    }
    /* Exact allocations make ASan catch a read past each truncated input. */
    for(size_t cut=0;cut<t.bytes;++cut) {
        unsigned char *small=malloc(cut?cut:1);assert(small);memcpy(small,data,cut);
        struct AROSEarlyFDT bad;assert(!AROSEarlyFDTOpen(&bad,small,cut));assert(!bad.blob);free(small);
    }
    data[0]^=1;assert(!AROSEarlyFDTOpen(&t,data,(size_t)n));data[0]^=1;
    /* Deterministic bounded mutation survey; valid survivors are walked too. */
    unsigned seed=0x41524f53,mutations=argc==3?2048:256;
    for(unsigned i=0;i<mutations;++i) {
        seed=seed*1664525+1013904223;size_t at=seed%(size_t)n;
        unsigned char saved=data[at];data[at]^=(unsigned char)(1U<<(i%8));
        if(AROSEarlyFDTOpen(&t,data,(size_t)n)) {
            int d=-1,k=-1;unsigned sampled=0;while((k=AROSEarlyFDTNext(&t,k,&d))>=0 && d>=0 && sampled++<16) {
                uint64_t a,s;struct AROSFDTIRQ irq;
                (void)AROSEarlyFDTReg(&t,k,0,&a,&s);
                (void)AROSEarlyFDTInterrupt(&t,k,0,&irq);
            }
        }
        data[at]=saved;
    }
    free(data);fprintf(stderr,"early-fdt PASS truncations=%ld mutations=%u\n",n,mutations);
}
