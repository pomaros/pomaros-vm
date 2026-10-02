/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu_topology.h"
#include "cpu_accounting.h"
int main(int argc, char **argv)
{
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    unsigned char blob[65536];size_t bytes=fread(blob,1,sizeof(blob),f);fclose(f);
    struct AROSEarlyFDT tree;struct AROSCPUTopology topology;
    assert(AROSEarlyFDTOpen(&tree,blob,bytes));
    assert(AROSCPUDiscover(&tree,0x100,&topology));
    assert(topology.count==3 && topology.boot_cpu==1);
    assert(!topology.cpu[2].enabled && topology.cpu[1].affinity==0x100);
    assert(topology.psci_available && topology.psci_hvc);
    assert(!AROSCPUDiscover(&tree,0x200,&topology)); /* disabled boot */
    assert(!AROSCPUDiscover(&tree,0x300,&topology)); /* absent boot */
    struct AROSCPUAccounting cpu={0};struct AROSCPUSample a,b;
    assert(AROSCPUSnapshot(&cpu,50,&a) && !a.state);
    AROSCPUAccount(&cpu,AROS_CPU_BUSY,100);
    assert(AROSCPUSnapshot(&cpu,100,&a));
    AROSCPUAccount(&cpu,AROS_CPU_IDLE,125);
    assert(AROSCPUSnapshot(&cpu,200,&b));
    assert(b.busy==25 && b.idle==75 && AROSCPULoad(&a,&b)==250);
    a=b;AROSCPUAccount(&cpu,AROS_CPU_BUSY,225);
    assert(AROSCPUSnapshot(&cpu,300,&b) && AROSCPULoad(&a,&b)==750);
    assert(!AROSCPUSnapshot(&cpu,200,&b)); /* timestamp older than owner */
    cpu.sequence|=1;assert(!AROSCPUSnapshot(&cpu,300,&b)); /* bounded retry */
    cpu.sequence++;AROSCPUAccount(&cpu,AROS_CPU_OFFLINE,300);
    assert(AROSCPUSnapshot(&cpu,400,&b) && AROSCPULoad(&a,&b)==-1);
    puts("PASS: sparse CPU affinities, disabled CPUs, firmware conduit, busy/idle deltas, bounded snapshots");
}
