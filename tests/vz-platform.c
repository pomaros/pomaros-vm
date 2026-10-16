// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../boot/arosboot/vz_platform.c"
EFI_STATUS arosboot_allocate_owned(struct EFI_BOOT_SERVICES *boot,size_t bytes,
    size_t alignment,uint32_t kind,uint32_t flags,uint32_t module,void **out)
{
    (void)boot;(void)bytes;(void)alignment;(void)kind;(void)flags;(void)module;(void)out;
    abort();
}
void arosboot_free_owned(struct EFI_BOOT_SERVICES *boot,void *address) {
    (void)boot;(void)address;abort();
}
static void put(uint8_t *p,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=(uint8_t)(value>>(i*8));
}
static void sum(uint8_t *p,unsigned size,unsigned offset) {
    p[offset]=0;uint8_t v=0;for(unsigned i=0;i<size;++i) v+=p[i];p[offset]=(uint8_t)-v;
}
int main(void) {
    char policy[16];
    const char *valid[]={"1\n","30\n","3600\n","forever\n","desktop\n"};
    const char *invalid[]={"","0\n","01\n","3601\n","-1\n","1","1\nX","forever","forever\nX","99999\n","desktop","desktop\nX"};
    for(unsigned i=0;i<sizeof(valid)/sizeof(*valid);++i) {
        strcpy(policy,valid[i]);assert(session_policy(policy,strlen(policy)));
    }
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        strcpy(policy,invalid[i]);assert(!session_policy(policy,strlen(policy)));
    }
    uint8_t rsdp[36]={0},xsdt[44]={0},mcfg[60]={0};
    memcpy(rsdp,"RSD PTR ",8);rsdp[15]=2;put(rsdp+20,36,4);put(rsdp+24,(uintptr_t)xsdt,8);
    memcpy(xsdt,"XSDT",4);put(xsdt+4,sizeof(xsdt),4);put(xsdt+36,(uintptr_t)mcfg,8);
    memcpy(mcfg,"MCFG",4);put(mcfg+4,sizeof(mcfg),4);put(mcfg+44,0x40000000,8);mcfg[55]=255;
    sum(mcfg,sizeof(mcfg),9);sum(xsdt,sizeof(xsdt),9);sum(rsdp,20,8);sum(rsdp,36,32);
    struct EFI_CONFIGURATION_TABLE table={{0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}},rsdp};
    struct EFI_SYSTEM_TABLE system={0};system.number_of_table_entries=1;system.configuration_table=&table;
    uint64_t address=0;uint32_t last_bus=0;
    assert(find_ecam(&system,&address,&last_bus)==0x40000000 && address==(uintptr_t)rsdp);
    rsdp[8]^=1;assert(find_ecam(&system,&address,&last_bus)==0);rsdp[8]^=1;
    mcfg[9]^=1;assert(find_ecam(&system,&address,&last_bus)==0);mcfg[9]^=1;
    mcfg[54]=1;sum(mcfg,sizeof(mcfg),9);assert(find_ecam(&system,&address,&last_bus)==0);
    mcfg[54]=0;sum(mcfg,sizeof(mcfg),9);
    put(xsdt+4,37,4);sum(xsdt,37,9);assert(find_ecam(&system,&address,&last_bus)==0);
    uint8_t *ecam=malloc(1<<20);assert(ecam);memset(ecam,255,1<<20);
    uint8_t *cfg=ecam+(5<<15);memset(cfg,0,256);put(cfg,0x10431af4,4);
    put(cfg+16,0x10004,4);put(cfg+20,1,4);cfg[0x34]=0x40;
    cfg[0x40]=9;cfg[0x41]=0x50;cfg[0x42]=16;cfg[0x43]=1;
    put(cfg+0x4c,56,4);
    cfg[0x50]=9;cfg[0x51]=0;cfg[0x52]=20;cfg[0x53]=2;
    put(cfg+0x58,0x4000,4);put(cfg+0x5c,16,4);put(cfg+0x60,4,4);
    struct VMConsole state={0};assert(find_console((uintptr_t)ecam,&state));
    assert(state.common==UINT64_C(0x100010000) && state.notify==UINT64_C(0x100014000));
    assert(state.region_end==UINT64_C(0x100015000));
    cfg[0x51]=0x50;assert(!find_console((uintptr_t)ecam,&state));cfg[0x51]=0;
    cfg[0x54]=6;assert(!find_console((uintptr_t)ecam,&state));cfg[0x54]=0;
    put(cfg+0x4c,8,4);assert(!find_console((uintptr_t)ecam,&state));
    _Alignas(8) unsigned char tree[4096];
    memset(tree,0,sizeof(tree));
    assert(make_tree(tree,sizeof(tree),&state,0x1000,NULL,NULL,NULL)==0);
    assert(fdt_check_full(tree,sizeof(tree))==0);
    assert(fdt_path_offset(tree,"/chosen")>0 && fdt_path_offset(tree,"/console")>0);
    int chosen=fdt_path_offset(tree,"/chosen"),length;
    assert(!fdt_getprop(tree,chosen,"aros,desktop-hold-seconds",&length));
    assert(make_tree(tree,sizeof(tree),&state,0x1000,NULL,NULL,"30")==0);
    chosen=fdt_path_offset(tree,"/chosen");
    assert(!strcmp(fdt_getprop(tree,chosen,"aros,desktop-hold-seconds",&length),"30"));
    assert(!fdt_getprop(tree,chosen,"aros,desktop-hold-forever",&length));
    assert(make_tree(tree,sizeof(tree),&state,0x1000,NULL,NULL,"forever")==0);
    chosen=fdt_path_offset(tree,"/chosen");
    assert(!strcmp(fdt_getprop(tree,chosen,"aros,desktop-hold-forever",&length),"1"));
    assert(!fdt_getprop(tree,chosen,"aros,desktop-hold-seconds",&length));
    assert(!fdt_getprop(tree,chosen,"aros,desktop-quiet",&length));
    assert(make_tree(tree,sizeof(tree),&state,0x1000,NULL,NULL,"desktop")==0);
    chosen=fdt_path_offset(tree,"/chosen");
    assert(!strcmp(fdt_getprop(tree,chosen,"aros,desktop-hold-forever",&length),"1"));
    assert(!strcmp(fdt_getprop(tree,chosen,"aros,desktop-quiet",&length),"1"));
    assert(!fdt_getprop(tree,chosen,"aros,desktop-hold-seconds",&length));
    unsigned char guard[128];memset(guard,0xa5,sizeof(guard));
    assert(make_tree(guard,64,&state,0x1000,NULL,NULL,NULL)==-FDT_ERR_NOSPACE);
    for(unsigned i=64;i<sizeof(guard);++i) assert(guard[i]==0xa5);
    uint8_t madt[132]={0},gtdt[104]={0};
    memcpy(madt,"APIC",4);put(madt+4,84,4);
    madt[44]=12;madt[45]=24;put(madt+52,0x10000000,8);madt[64]=3;
    madt[68]=14;madt[69]=16;put(madt+72,0x10010000,8);put(madt+80,0x20000,4);
    memcpy(gtdt,"GTDT",4);put(gtdt+4,sizeof(gtdt),4);
    put(gtdt+56,30,4);put(gtdt+64,27,4);
    sum(madt,84,9);sum(gtdt,sizeof(gtdt),9);
    struct VZInterrupts irq;
    assert(parse_interrupts(madt,gtdt,&irq));
    assert(irq.distributor==0x10000000 && irq.redistributors==0x10010000);
    assert(irq.timer[0]==30 && irq.timer[1]==27 && irq.flags[1]==4);
    assert(make_tree(tree,sizeof(tree),&state,0x1000,&irq,NULL,NULL)==0);
    int timer=fdt_path_offset(tree,"/timer");
    assert(fdt_stringlist_search(tree,timer,"interrupt-names","virt")==1);
    const fdt32_t *cells=fdt_getprop(tree,timer,"interrupts",NULL);
    assert(cells && fdt32_to_cpu(cells[4])==11);
    madt[45]=0;sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[45]=99;sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[45]=24;madt[64]=2;sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[64]=3;put(madt+72,0x10000000,8);sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+72,0x10010001,8);sum(madt,84,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+72,0x10010000,8);sum(madt,84,9);
    put(gtdt+64,15,4);sum(gtdt,sizeof(gtdt),9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(gtdt+64,27,4);put(gtdt+68,2,4);sum(gtdt,sizeof(gtdt),9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(gtdt+68,8,4);sum(gtdt,sizeof(gtdt),9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(gtdt+68,1,4);sum(gtdt,sizeof(gtdt),9);assert(parse_interrupts(madt,gtdt,&irq) && irq.flags[1]==1);
    gtdt[9]^=1;assert(!parse_interrupts(madt,gtdt,&irq));gtdt[9]^=1;
    memcpy(madt+84,madt+68,16);put(madt+4,100,4);sum(madt,100,9);
    assert(!parse_interrupts(madt,gtdt,&irq));
    uint8_t cpu_madt[244]={0},fadt[276]={0};
    memcpy(cpu_madt,madt,84);put(cpu_madt+4,sizeof(cpu_madt),4);
    memcpy(fadt,"FACP",4);put(fadt+4,sizeof(fadt),4);put(fadt+129,3,2);
    for (unsigned i=0;i<2;i++) {
        unsigned at=84+80*i;cpu_madt[at]=11;cpu_madt[at+1]=80;
        put(cpu_madt+at+12,1,4);put(cpu_madt+at+68,i?0x100:0,8);
    }
    sum(cpu_madt,sizeof(cpu_madt),9);sum(fadt,sizeof(fadt),9);
    assert(parse_cpus(cpu_madt,fadt,&irq) && irq.cpu_count==2 && irq.psci_hvc);
    assert(irq.mpidr[1]==0x100);
    assert(make_tree(tree,sizeof(tree),&state,0x1000,&irq,NULL,NULL)==0);
    assert(fdt_path_offset(tree,"/cpus/cpu@100")>0);
    assert(!strcmp(fdt_getprop(tree,fdt_path_offset(tree,"/psci"),"method",NULL),"hvc"));
    put(cpu_madt+164+68,0,8);sum(cpu_madt,sizeof(cpu_madt),9);
    assert(!parse_cpus(cpu_madt,fadt,&irq));
    put(cpu_madt+164+68,0x100,8);put(cpu_madt+164+12,0,4);sum(cpu_madt,sizeof(cpu_madt),9);
    assert(parse_cpus(cpu_madt,fadt,&irq) && irq.cpu_count==1);
    put(cpu_madt+164+12,1,4);sum(cpu_madt,sizeof(cpu_madt),9);
    put(fadt+129,0,2);sum(fadt,sizeof(fadt),9);assert(!parse_cpus(cpu_madt,fadt,&irq));
    put(fadt+129,1,2);sum(fadt,sizeof(fadt),9);assert(parse_cpus(cpu_madt,fadt,&irq) && !irq.psci_hvc);
    cpu_madt[9]^=1;assert(!parse_cpus(cpu_madt,fadt,&irq));cpu_madt[9]^=1;
    uint8_t irq_xsdt[68]={0};
    memcpy(irq_xsdt,"XSDT",4);put(irq_xsdt+4,sizeof(irq_xsdt),4);
    put(irq_xsdt+36,(uintptr_t)mcfg,8);put(irq_xsdt+44,(uintptr_t)cpu_madt,8);
    put(irq_xsdt+52,(uintptr_t)gtdt,8);
    put(irq_xsdt+60,(uintptr_t)fadt,8);
    put(madt+4,84,4);sum(madt,84,9);sum(irq_xsdt,sizeof(irq_xsdt),9);
    put(rsdp+24,(uintptr_t)irq_xsdt,8);sum(rsdp,36,32);
    assert(find_interrupts((uintptr_t)rsdp,&irq));
    put(irq_xsdt+36,(uintptr_t)gtdt,8);sum(irq_xsdt,sizeof(irq_xsdt),9);
    assert(!find_interrupts((uintptr_t)rsdp,&irq));
    put(irq_xsdt+36,(uintptr_t)mcfg,8);put(irq_xsdt+44,(uintptr_t)mcfg,8);
    sum(irq_xsdt,sizeof(irq_xsdt),9);assert(!find_interrupts((uintptr_t)rsdp,&irq));
    uint8_t resources[46*3+2]={0};
    for(unsigned i=0;i<3;++i) { resources[46*i]=0x8a;put(resources+46*i+1,43,2); }
    resources[3]=2;put(resources+22,255,8);put(resources+38,256,8);
    uint8_t *mem=resources+46;
    put(mem+6,32,8);put(mem+14,0x50000000,8);put(mem+22,0x5fffffff,8);put(mem+38,0x10000000,8);
    mem+=46;mem[5]=6;put(mem+6,64,8);put(mem+14,UINT64_C(0x100000000),8);
    put(mem+22,UINT64_C(0x1ffffffff),8);put(mem+38,UINT64_C(0x100000000),8);
    resources[138]=0x79;
    struct VZPCI pci;
    assert(parse_pci_resources(resources,sizeof(resources),&pci));
    assert(pci.count==2 && pci.last_bus==255 && pci.ranges[1].flags==0x43000000);
    for(size_t n=0;n<sizeof(resources);++n) assert(!parse_pci_resources(resources,n,&pci));
    pci.ecam=0x40000000;
    assert(make_tree(tree,sizeof(tree),&state,0x1000,NULL,&pci,NULL)==0);
    int bus=fdt_path_offset(tree,"/pcie");
    const fdt32_t *ranges=fdt_getprop(tree,bus,"ranges",&length);
    assert(ranges && length==56 && fdt32_to_cpu(ranges[7])==0x43000000 &&
        fdt32_to_cpu(ranges[8])==1 && fdt32_to_cpu(ranges[10])==1);
    /* MADT MSI frame: flags=0 ignores even nonsense count/base fields.
     * This matches VZ firmware, which supplies a frame without an override. */
    memset(madt+84,0,48);put(madt+4,108,4);
    madt[84]=13;madt[85]=24;put(madt+92,0x1fff0000,8);
    put(madt+104,0xffff,2);put(madt+106,0xffff,2);sum(madt,108,9);
    assert(parse_interrupts(madt,gtdt,&irq));
    assert(irq.msi_frame==0x1fff0000 && !irq.msi_base && !irq.msi_count);
    assert(make_tree(tree,sizeof(tree),&state,0x1000,&irq,&pci,NULL)==0);
    const char *fdt_output=getenv("AROS_VZ_FDT_OUTPUT");
    if(fdt_output) {
        FILE *file=fopen(fdt_output,"wb");assert(file);
        assert(fwrite(tree,1,fdt_totalsize(tree),file)==(size_t)fdt_totalsize(tree));
        assert(fclose(file)==0);
    }
    int frame=fdt_path_offset(tree,"/interrupt-controller/v2m@1fff0000");
    assert(frame>0 && fdt_node_check_compatible(tree,frame,"arm,gic-v2m-frame")==0);
    const fdt64_t *frame_reg=fdt_getprop(tree,frame,"reg",&length);
    assert(frame_reg && length==16 && fdt64_ld(frame_reg)==0x1fff0000 &&
        fdt64_ld(frame_reg+1)==4096);
    assert(fdt_getprop(tree,frame,"msi-controller",&length) && length==0);
    assert(!fdt_getprop(tree,frame,"arm,msi-base-spi",NULL));
    bus=fdt_path_offset(tree,"/pcie");
    cells=fdt_getprop(tree,bus,"msi-parent",&length);
    assert(cells && length==4 && fdt32_to_cpu(cells[0])==2);
    assert(fdt_node_offset_by_phandle(tree,2)==frame);
    put(madt+100,1,4);put(madt+104,128,2);put(madt+106,128,2);sum(madt,108,9);
    assert(parse_interrupts(madt,gtdt,&irq) && irq.msi_base==128 && irq.msi_count==128);
    assert(make_tree(tree,sizeof(tree),&state,0x1000,&irq,&pci,NULL)==0);
    frame=fdt_path_offset(tree,"/interrupt-controller/v2m@1fff0000");
    cells=fdt_getprop(tree,frame,"arm,msi-num-spis",&length);
    assert(cells && length==4 && fdt32_to_cpu(*cells)==128);
    cells=fdt_getprop(tree,frame,"arm,msi-base-spi",&length);
    assert(cells && length==4 && fdt32_to_cpu(*cells)==128);
    /* Reserved bits, invalid ranges, alignment/overflow and aliasing fail. */
    put(madt+100,2,4);sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+100,1,4);put(madt+104,0,2);sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+104,128,2);put(madt+106,31,2);sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+106,1000,2);sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+106,128,2);madt[86]=1;sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[86]=0;
    const uint64_t invalid_frames[]={0,0x1fff0001,UINT64_MAX-4095,0x10000000,0x1002f000};
    for(unsigned i=0;i<sizeof(invalid_frames)/sizeof(invalid_frames[0]);++i) {
        put(madt+92,invalid_frames[i],8);sum(madt,108,9);
        assert(!parse_interrupts(madt,gtdt,&irq));
    }
    put(madt+92,0x1fff0000,8);
    madt[85]=23;sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[85]=25;sum(madt,108,9);assert(!parse_interrupts(madt,gtdt,&irq));
    madt[85]=24;
    memcpy(madt+108,madt+84,24);put(madt+4,132,4);sum(madt,132,9);
    assert(!parse_interrupts(madt,gtdt,&irq));
    put(madt+4,84,4);sum(madt,84,9);assert(parse_interrupts(madt,gtdt,&irq));
    assert(make_tree(tree,sizeof(tree),&state,0x1000,&irq,&pci,NULL)==0);
    assert(fdt_path_offset(tree,"/interrupt-controller/v2m@1fff0000")==-FDT_ERR_NOTFOUND);
    bus=fdt_path_offset(tree,"/pcie");assert(!fdt_getprop(tree,bus,"msi-parent",NULL));
    mem[30]=1;assert(!parse_pci_resources(resources,sizeof(resources),&pci));mem[30]=0;
    mem[14]=1;assert(!parse_pci_resources(resources,sizeof(resources),&pci));mem[14]=0;
    mem[1]=42;assert(!parse_pci_resources(resources,sizeof(resources),&pci));mem[1]=43;
    mem[3]=2;assert(!parse_pci_resources(resources,sizeof(resources),&pci));mem[3]=0;
    resources[139]=1;assert(!parse_pci_resources(resources,sizeof(resources),&pci));
    sum(resources,sizeof(resources),139);assert(parse_pci_resources(resources,sizeof(resources),&pci));
    free(ecam);
    puts("vz-platform result=PASS groups=ACPI-PCI,root-apertures,IRQ-bindings,MSI-frame,FDT");
}
