#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Compare generic early-reader paths and scalar reg values with fdtget."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--tree',type=Path,action='append',default=[])
p.add_argument('--output',type=Path)
a=p.parse_args()
core=Path(os.environ.get('AROS_CORE_SOURCE',root.parent/'aros-core'))
fdt=core/'arch/aarch64-all/boot/fdt'
rows=[]
def run(args,**kw): return subprocess.run(args,check=True,text=True,capture_output=True,**kw).stdout.strip()
def prop(path,node,key):
 r=subprocess.run(['fdtget','-t','x',str(path),node,key],text=True,capture_output=True)
 return [int(x,16) for x in r.stdout.split()] if r.returncode==0 else None
def number(path,node,key,default):
 value=prop(path,node,key);return value[0] if value and len(value)==1 else default
with tempfile.TemporaryDirectory() as temp:
 work=Path(temp);exe=work/'reader'
 sources=[fdt/'early_fdt.c',*[fdt/'libfdt'/n for n in ['fdt.c','fdt_ro.c','fdt_check.c']]]
 run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
      '-I'+str(fdt),'-I'+str(fdt/'libfdt'),str(root/'tests/early-fdt.c'),*map(str,sources),'-o',str(exe)])
 fixture=work/'portable.dtb'
 run(['dtc','-q','-I','dts','-O','dtb','-o',str(fixture),str(root/'tests/fdt/portable.dts')])
 for path in [fixture,*a.tree]:
  result=subprocess.run([str(exe),str(path),*(['exercise'] if path==fixture else [])],capture_output=True,text=True,timeout=60)
  if result.returncode: raise RuntimeError(result.stdout+'\n'+result.stderr)
  fields=[line.split('\t') for line in result.stdout.splitlines()]
  actual_paths={f[1] for f in fields if f[0]=='NODE'}
  expected_paths=set();todo=['/']
  while todo:
   node=todo.pop();expected_paths.add(node)
   children=run(['fdtget','-l',str(path),node]).splitlines()
   todo += [node.rstrip('/')+'/'+c for c in children]
  assert actual_paths==expected_paths,(path,actual_paths^expected_paths)
  reg_count=0
  for row in fields:
   if row[0]!='REG': continue
   _,node,index,address,size=row;index=int(index)
   parent=node.rsplit('/',1)[0] or '/'
   ac=number(path,parent,'#address-cells',2);sc=number(path,parent,'#size-cells',1)
   values=prop(path,node,'reg');assert values is not None
   entry=values[index*(ac+sc):(index+1)*(ac+sc)]
   def value(cells):
    out=0
    for c in cells: out=(out<<32)|c
    return out
   assert int(address,16)==value(entry[:ac]) and int(size,16)==value(entry[ac:])
   reg_count+=1
  row={'tree':str(path) if path!=fixture else 'tests/fdt/portable.dts','sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
       'nodes':len(actual_paths),'reg_entries':reg_count,'result':'PASS','lane':'host-parser-ASan-UBSan'}
  rows.append(row);print(json.dumps(row))
if a.output:
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(rows,indent=2)+'\n')
