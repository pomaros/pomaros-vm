// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include "stage2_tags.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    struct AROSAppleTagItem tags[AROS_APPLE_MAX_BOOT_TAGS] = {0};
    struct AROSAppleStage2Result result = {0};
    result.boot_tags=(uintptr_t)tags;result.boot_tag_count=1;
    result.device_map.profile=AROS_APPLE_DEVICE_MAP_PROFILE_VIRT;
    result.device_map.role_count=3;
    result.device_map.roles[0]=(struct AROSAppleDeviceRole){AROS_APPLE_DEVICE_ROLE_CONSOLE,0,0x9000000,0x9001000};
    result.device_map.roles[1]=(struct AROSAppleDeviceRole){AROS_APPLE_DEVICE_ROLE_GIC,0,0x8000000,0x8010000};
    result.device_map.roles[2]=(struct AROSAppleDeviceRole){AROS_APPLE_DEVICE_ROLE_GIC,1,0x80a0000,0x80c0000};
    assert(AROSAppleStage2AppendDeviceTags(&result));
    assert(result.boot_tag_count==7);
    for(unsigned i=0;i<4;++i) assert(tags[2+i].tag==AROS_APPLE_KRN_GIC_DIST_START+i);
    assert(tags[2].data==0x8000000 && tags[5].data==0x80c0000);
    assert(tags[6].tag==AROS_APPLE_TAG_DONE);
    assert(!AROSAppleStage2AppendDeviceTags(&result));
    /* Missing one GIC role cannot produce a partial contract. */
    memset(tags,0,sizeof(tags));result.boot_tag_count=1;result.device_map.role_count=2;
    assert(!AROSAppleStage2AppendDeviceTags(&result));
    result.device_map.role_count=3;result.boot_tag_count=AROS_APPLE_MAX_BOOT_TAGS-5;
    assert(!AROSAppleStage2AppendDeviceTags(&result));
    /* Parser rejects incomplete, duplicate and overlapping records before use. */
    memset(tags,0,sizeof(tags));tags[0].tag=AROS_APPLE_KRN_GIC_DIST_START;tags[0].data=0x8000000;
    assert(AROSAppleKernelStartupContract(tags,AROS_APPLE_BOOT_MAGIC)==AROS_APPLE_STARTUP_CONTRACT_LAYOUT);
    tags[1]=tags[0];
    assert(AROSAppleKernelStartupContract(tags,AROS_APPLE_BOOT_MAGIC)==AROS_APPLE_STARTUP_CONTRACT_DUPLICATE);
    for(unsigned i=0;i<4;++i) tags[i]=(struct AROSAppleTagItem){AROS_APPLE_KRN_GIC_DIST_START+i,0,0x8000000+(i%2)*0x10000};
    assert(AROSAppleKernelStartupContract(tags,AROS_APPLE_BOOT_MAGIC)==AROS_APPLE_STARTUP_CONTRACT_LAYOUT);
    puts("gic-contract result=PASS append,capacity,missing,duplicate,overlap");
}
