#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Run the real command against a synthetic public OF API under ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
core = Path(os.environ.get('AROS_CORE_SOURCE', root.parent / 'aros-core'))
with tempfile.TemporaryDirectory(prefix='devicetree-') as directory:
    tmp = Path(directory)
    header = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef void *APTR;
typedef unsigned long ULONG;
typedef long LONG;
typedef uintptr_t IPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DOSFALSE 0
#define RETURN_OK 0
#define RETURN_WARN 5
#define RETURN_ERROR 10
#define RETURN_FAIL 20
#define SIGBREAKF_CTRL_C 4096
struct RDArgs { int unused; };
struct Resident { unsigned char rt_Version; };
struct RDArgs *ReadArgs(const char *, IPTR *, void *);
void FreeArgs(struct RDArgs *);
LONG IoErr(void);
void PrintFault(LONG,const char *);
int Printf(const char *, ...);
int PutStr(const char *);
APTR OpenResource(const char *);
struct Resident *FindResident(const char *);
ULONG SetSignal(ULONG,ULONG);
LONG Flush(APTR);
APTR Output(void);
APTR OF_OpenKey(char *);
void OF_CloseKey(APTR);
APTR OF_GetProperty(APTR,APTR);
APTR OF_GetChild(APTR,APTR);
APTR OF_FindProperty(APTR,char *);
const char *OF_GetNodeName(APTR);
const char *OF_GetPropName(APTR);
ULONG OF_GetPropLen(APTR);
APTR OF_GetPropValue(APTR);
'''
    for file in ('exec/types.h','exec/resident.h','dos/dos.h','proto/exec.h','proto/dos.h','proto/openfirmware.h'):
        p = tmp / file
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "shim.h"\n')
    (tmp / 'shim.h').write_text(header)
    source = r'''
#include "shim.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define main command_main
#include "COMMAND_SOURCE"
#undef main
struct Property { const char *name; unsigned long size; void *data; };
static unsigned char cells[]={99,0,0,0,42};
static unsigned char large[257];
static struct Property props[]={
 {"model",8,"Test VM"},{"compatible",8,"one\0two"},
 {"reg",4,cells+1},{"odd",3,"abc"},{"status",3,"bad"},
 {"flag",0,NULL},{"large",257,large},{"bootargs",7,"SECRET"},
 {"unsafe\033",4,"a\033b"}
};
static int mode,rootnode,childnode;
static struct Resident resident={1};
struct RDArgs *ReadArgs(const char *t, IPTR *a, void *p) {
 static struct RDArgs r; static LONG depth;
 (void)t;(void)p;
 if(mode==1) a[1]=1;
 if(mode==2) {a[2]=(IPTR)"compatible";a[3]=1;}
 if(mode==3) a[0]=(IPTR)"/missing";
 if(mode==4) a[2]=(IPTR)"missing";
 if(mode==5) {depth=0;a[4]=(IPTR)&depth;}
 if(mode==6) {depth=65;a[4]=(IPTR)&depth;}
 if(mode==7) a[2]=(IPTR)"bootargs";
 if(mode==11) {a[0]=(IPTR)"/pci@0/";a[1]=1;}
 if(mode==13) a[2]=(IPTR)"reg";
 return &r;
}
void FreeArgs(struct RDArgs *r){(void)r;}
LONG IoErr(void){return 0;}
void PrintFault(LONG e,const char *s){(void)e;(void)s;}
int Printf(const char *s,...){va_list a;va_start(a,s);int n=vprintf(s,a);va_end(a);return n;}
int PutStr(const char *s){return fputs(s,stdout);}
APTR OpenResource(const char *s){(void)s;return mode==8?NULL:&rootnode;}
struct Resident *FindResident(const char *s){(void)s;resident.rt_Version=mode==9?0:1;return &resident;}
ULONG SetSignal(ULONG a,ULONG b){(void)a;(void)b;return mode==10?SIGBREAKF_CTRL_C:0;}
LONG Flush(APTR f){(void)f;return mode==12?0:1;}
APTR Output(void){return stdout;}
APTR OF_OpenKey(char *s){return !strcmp(s,"/")?&rootnode:!strcmp(s,"/pci@0")?&childnode:NULL;}
void OF_CloseKey(APTR n){(void)n;}
APTR OF_GetProperty(APTR n,APTR p){if(n!=&rootnode)return NULL;struct Property *v=p?(struct Property *)p+1:props;return v<props+9?v:NULL;}
APTR OF_GetChild(APTR n,APTR p){return n==&rootnode&&!p?&childnode:NULL;}
APTR OF_FindProperty(APTR n,char *s){APTR p=NULL;while((p=OF_GetProperty(n,p)))if(!strcmp(OF_GetPropName(p),s))return p;return NULL;}
const char *OF_GetNodeName(APTR n){return n==&rootnode?"":"pci@0";}
const char *OF_GetPropName(APTR p){return ((struct Property *)p)->name;}
ULONG OF_GetPropLen(APTR p){return ((struct Property *)p)->size;}
APTR OF_GetPropValue(APTR p){return ((struct Property *)p)->data;}
int main(int argc,char **argv){(void)argc;mode=atoi(argv[1]);memset(large,0xaa,sizeof large);return command_main();}
'''
    (tmp / 'test.c').write_text(source.replace('COMMAND_SOURCE', str(core / 'workbench/c/DeviceTree/main.c')))
    exe = tmp / 'test'
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(tmp),str(tmp/'test.c'),'-o',str(exe)],check=True)
    def run(mode, code=0):
        r=subprocess.run([str(exe),str(mode)],capture_output=True,text=True)
        assert r.returncode==code,(mode,r.returncode,r.stdout,r.stderr)
        assert not r.stderr,(mode,r.stderr)
        assert '\x1b' not in r.stdout and 'SECRET' not in r.stdout
        return r.stdout
    assert '"one", "two"' in run(0) and '0x0000002a' in run(0)
    out=run(1)
    assert '<present>' in out and 'first 256 of 257 bytes' in out and 'unsafe\\x1b' in out
    assert '[62 61 64]' in out and 'withheld' in out
    assert '[6f 6e 65 00 74 77 6f 00]' in run(2)
    assert 'node not found' in run(3,10)
    assert 'property not found' in run(4,10)
    assert '1 node(s)' in run(5) and '/pci@0' not in run(5)
    assert 'DEPTH' in run(6,10)
    assert 'withheld' in run(7)
    assert 'no OpenFirmware' in run(8,20)
    assert 'version 1' in run(9,20)
    run(10,5)
    assert '/pci@0\n' in run(11)
    run(12,10)
    assert '0x0000002a' in run(13)
    print('DeviceTree command PASS: 14 scenarios, ASan/UBSan, unaligned cells, escaped output, redaction, bounds and errors')
