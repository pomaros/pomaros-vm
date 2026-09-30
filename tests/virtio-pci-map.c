// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pci_mapping.h"
struct Fixture {
    uint8_t config[256];
    _Alignas(8) uint8_t cpu[256];
    unsigned maps,unmaps,live,fail_map;
    uint64_t bus,size;
};
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=v>>(8*i); }
static uint32_t read_config(void *context,unsigned reg,unsigned width)
{
    struct Fixture *f=context;assert(reg+width<=256);
    uint32_t value=0;for(unsigned i=0;i<width;++i) value|=(uint32_t)f->config[reg+i]<<(8*i);
    return value;
}
static int bar(void *context,unsigned n,uint64_t *base,uint64_t *size)
{ struct Fixture *f=context;if(n) return 0;*base=f->bus;*size=f->size;return 1; }
static void *map(void *context,uint64_t bus,uint32_t bytes)
{
    struct Fixture *f=context;
    if(++f->maps==f->fail_map) return (void *)-1;
    assert(bus>=f->bus && bus-f->bus+bytes<=sizeof(f->cpu));
    ++f->live;return f->cpu+(bus-f->bus);
}
static void unmap(void *context,void *cpu,uint32_t bytes)
{
    struct Fixture *f=context;assert(f->live && cpu>=(void *)f->cpu);
    assert((uint8_t *)cpu+bytes<=f->cpu+sizeof(f->cpu));
    --f->live;++f->unmaps;
}
static void setup(struct Fixture *f)
{
    memset(f,0,sizeof(*f));f->bus=0x12340000;f->size=256;f->config[0x34]=0x40;
    for(unsigned i=0;i<4;++i) {
        uint8_t *c=f->config+0x40+20*i;
        c[0]=9;c[1]=i==3?0:0x40+20*(i+1);c[2]=i==1?20:16;c[3]=i+1;
        put32(c+8,i*64);put32(c+12,i==0?56:i==1?16:i==2?1:32);
        if(i==1) put32(c+16,2);
    }
}
int main(void)
{
    struct Fixture f;struct VirtioPCIMapping m={0};
    struct VirtioPCIAccess io={&f,read_config,bar,map,unmap};
    setup(&f);assert(VirtioPCIMap(&io,&m,15));
    assert(m.regions[0]==f.cpu && m.regions[3]==f.cpu+192 && f.live==4);
    assert((uintptr_t)m.regions[0]!=f.bus); /* CPU != PCI address. */
    assert(VirtioPCINotify(&m,7)==f.cpu+78 && !VirtioPCINotify(&m,8));
    m.notify_multiplier=UINT32_MAX;assert(!VirtioPCINotify(&m,UINT16_MAX));
    assert(!VirtioPCIMap(&io,&m,15));assert(f.maps==4);
    VirtioPCIUnmap(&io,&m);assert(!f.live && f.unmaps==4);
    for(unsigned fail=1;fail<=4;++fail) {
        setup(&f);f.fail_map=fail;assert(!VirtioPCIMap(&io,&m,15));
        assert(!f.live && f.unmaps==fail-1 && !m.regions[0]);
    }
    for(unsigned which=0;which<9;++which) {
        setup(&f);
        switch(which) {
        case 0:f.config[0x40+60+1]=0x40;break; /* cycle after four maps */
        case 1:f.config[0x34]=0x41;break;
        case 2:f.config[0x54+2]=16;break; /* notify multiplier missing */
        case 3:put32(f.config+0x68+8,256);break; /* BAR end */
        case 4:put32(f.config+0x40+12,55);break;
        case 5:f.config[0x68+4]=6;break;
        case 6:f.config[0x68+1]=0;break; /* required device config missing */
        case 7:f.bus=UINT64_MAX-32;break;
        case 8:f.config[0x34]=0xfc;f.config[0xfc]=9;f.config[0xfe]=16;f.config[0xff]=1;break;
        }
        assert(!VirtioPCIMap(&io,&m,15));assert(!f.live && !m.regions[0]);
    }
    puts("virtio-pci-map result=PASS translated-addresses,bounds,cycles,partial-unwind,notify-overflow");
}
