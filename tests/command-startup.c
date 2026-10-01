// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/tasks.h>
static void mark(const char *s) { while (*s) RawPutChar(*s++); }
static void hex(IPTR n) {
    for (int bit=(int)sizeof(n)*8-4;bit>=0;bit-=4)
        RawPutChar("0123456789abcdef"[(n>>bit)&15]);
}
static LONG swapped(IPTR a, IPTR b) { return (LONG)(a+b); }
int test_command_startup(void)
{
    BPTR request=Lock((CONST_STRPTR)"SYS:COMMAND.REQ",ACCESS_READ);
    if (!request) return 1;
    UnLock(request);
    UBYTE *stack=AllocMem(65536,MEMF_ANY);
    if (!stack) return 0;
    struct StackSwapStruct sss={stack,stack+65536,stack+65536};
    struct StackSwapArgs args={{19,23,0,0,0,0,0,0}};
    mark("[VM] command NewStackSwap begin\n");
    IPTR result=NewStackSwap(&sss,(LONG_FUNC)swapped,&args);
    FreeMem(stack,65536);
    if (result!=42) return 0;
    mark("[VM] command NewStackSwap PASS\n");
    BPTR program=LoadSeg((CONST_STRPTR)"SYS:C/VMExchange");
    if (!program) return 0;
    ULONG *entry=(ULONG *)((UBYTE *)BADDR(program)+sizeof(BPTR));
    mark("[VM] command loaded entry=");hex((IPTR)entry);
    mark(" words=");hex(entry[0]);RawPutChar(' ');hex(entry[1]);RawPutChar('\n');
    UBYTE arguments[]="HELLO\n";
    result=RunCommand(program,65536,arguments,6);
    UnLoadSeg(program);
    /* No vsock device is packaged: HELLO must reach main and fail cleanly. */
    if (result!=20) return 0;
    mark("[VM] command startup without service PASS\n");
    return 1;
}
