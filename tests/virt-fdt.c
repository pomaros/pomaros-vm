// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include "stage2_fdt_map.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint64_t AROSVMPL011Base;
extern void AROSVMConsoleWrite0(const char *text);
struct Plan { struct AROSAppleMemoryPlan header; struct AROSAppleMemoryPlanRange range; };
int main(int argc, char **argv) {
    assert(argc == 6);
    FILE *f = fopen(argv[1],"rb"); assert(f);
    unsigned char dtb[65536]; size_t n=fread(dtb,1,sizeof(dtb),f); fclose(f);
    assert(n>0 && n<sizeof(dtb));
    struct Plan plan={0};
    plan.header.version=AROS_APPLE_MEMORY_PLAN_VERSION;
    plan.header.size=sizeof(plan);
    plan.header.entry_size=sizeof(plan.range); plan.header.entry_count=1;
    plan.header.conventional_count=1;plan.header.conventional_bytes=0x20000000;
    plan.range.start=0x40000000;plan.range.end=0x60000000;
    plan.range.kind=AROS_APPLE_MEMORY_RANGE_CONVENTIONAL;
    struct AROSBootInfo boot={0};
    boot.fdt_physical=(uintptr_t)dtb;boot.fdt_size=n;
    boot.flags=AROS_BOOTINFO_FLAG_FDT;
    struct AROSAppleDeviceMap map;
    int result=AROSAppleStage2BuildDeviceMap(&boot,&plan.header,&map);
    assert(result == atoi(argv[2]));
    if(result) {
        assert(map.profile==AROS_APPLE_DEVICE_MAP_PROFILE_VIRT);
        assert(map.entry_count==(unsigned)atoi(argv[4]));
        assert(map.role_count==(unsigned)atoi(argv[5]));
        assert(AROSVMPL011Base==strtoull(argv[3],NULL,0));
        assert(map.roles[0].start==AROSVMPL011Base);
        if (map.role_count == 3) {
            assert(map.roles[1].kind == AROS_APPLE_DEVICE_ROLE_GIC);
            assert(map.roles[1].instance == 0);
            assert(map.roles[2].kind == AROS_APPLE_DEVICE_ROLE_GIC);
            assert(map.roles[2].instance == 1);
            assert(map.roles[1].start == 0x08000000);
        }
        assert(map.roles[0].kind==AROS_APPLE_DEVICE_ROLE_CONSOLE);
    }
    /* A stuck FIFO must not trap the loader in its diagnostic path. */
    uint32_t uart[8]={0};
    uart[6]=1U<<5;
    AROSVMPL011Base=(uintptr_t)uart;
    AROSVMConsoleWrite0("X");
    assert(uart[0]==0);
    uart[6]=0;
    AROSVMConsoleWrite0("X");
    assert(uart[0]=='X');
    AROSVMPL011Base=0;
    puts("virt-fdt case=PASS console-bounded=PASS");
}
