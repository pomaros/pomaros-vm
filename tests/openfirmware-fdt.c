// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "of_intern.h"
#define PROTO_EXEC_H
static unsigned allocations, live, fail_at;
static APTR AllocVec(ULONG bytes, ULONG flags)
{
    (void)flags;
    if (++allocations == fail_at) return NULL;
    void *p=calloc(1,bytes);assert(p);++live;return p;
}
static void FreeVec(APTR p) { assert(live);--live;free(p); }
#include "of_fdt.c"
static unsigned compare_tree(of_node_t *node, const void *blob, int offset)
{
    assert(!strcmp(node->on_name,fdt_get_name(blob,offset,NULL)));
    unsigned count=1;
    int prop;
    of_property_t *actual=(void *)node->on_properties.mlh_Head;
    fdt_for_each_property_offset(prop,blob,offset) {
        const char *name;int bytes;
        const void *data=fdt_getprop_by_offset(blob,prop,&name,&bytes);
        assert(actual->op_node.mln_Succ && !strcmp(actual->op_name,name));
        assert(actual->op_length==(unsigned)bytes && !memcmp(actual->op_value,data,bytes));
        actual=(void *)actual->op_node.mln_Succ;
    }
    assert(!actual->op_node.mln_Succ);
    int child;
    of_node_t *actual_child=(void *)node->on_children.mlh_Head;
    fdt_for_each_subnode(child,blob,offset) {
        assert(actual_child->on_node.mln_Succ);
        count+=compare_tree(actual_child,blob,child);
        actual_child=(void *)actual_child->on_node.mln_Succ;
    }
    assert(!actual_child->on_node.mln_Succ);
    return count;
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    unsigned char blob[65536];size_t bytes=fread(blob,1,sizeof(blob),f);fclose(f);
    assert(bytes>40 && bytes<sizeof(blob));
    of_node_t *root=OFBuildFDT(blob);assert(root);
    unsigned total=allocations,nodes=compare_tree(root,blob,0);
    free_tree(root);assert(!live);
    for(unsigned fail=1;fail<=total;++fail) {
        allocations=0;fail_at=fail;
        assert(!OFBuildFDT(blob));assert(!live);
    }
    fail_at=0;allocations=0;blob[0]^=1;
    assert(!OFBuildFDT(blob) && !allocations);
    printf("openfirmware-fdt result=PASS nodes=%u allocation-failures=%u leaks=0\n",nodes,total);
}
