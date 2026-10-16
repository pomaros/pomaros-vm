#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise real genmodule include generation with short names under ASan/UBSan."""
import argparse, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'aros-core')
p.add_argument('--output',type=Path,required=True)
p.add_argument('--expect-failure',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=a.source.resolve();out=a.output.resolve();binary=out/'genmodule'
compiled=subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
    *map(str,sorted((source/'tools/genmodule').glob('*.c'))),'-o',str(binary)],capture_output=True)
(out/'compile.log').write_bytes(compiled.stdout+compiled.stderr)
compiled.check_returncode()
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
results=[]
for name,config in [('graphics',source/'rom/graphics/graphics.conf'),('short',out/'short.conf')]:
    if name=='short':
        config.write_text('##begin config\nversion 1.0\nlibbase ShortBase\n##end config\n##begin functionlist\n'+
            ''.join('void '+f+'(APTR value) (A0)\n' for f in ['X','Go','Text','Short','Sixsix'])+
            'void SubmitA(APTR object, struct TagItem *tags) (A0, A1)\n'+
            'void SubmitTagList(APTR object, struct TagItem *tags) (A0, A1)\n'+
            'void PrintArgs(APTR object, RAWARG args) (A0, A1)\n##end functionlist\n')
    dest=out/name
    for sub in ['proto','clib','defines','inline','interfaces']:(dest/sub).mkdir(parents=True,exist_ok=True)
    r=subprocess.run([str(binary),'-c',str(config),'-d',str(dest),'writeincludes',name,'library'],env=env,capture_output=True)
    (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
    failed=b'AddressSanitizer' in r.stderr or b'runtime error:' in r.stderr
    okay=r.returncode!=0 and failed if a.expect_failure else r.returncode==0 and not failed
    if okay and not a.expect_failure and name=='short':
        for sub in ['defines','inline']:
            text=(dest/sub/'short.h').read_text()
            okay=okay and all(word in text for word in ['Text','Sixsix','SubmitTags','Print'])
    results.append({'case':name,'passed':okay,'exit':r.returncode,'sanitizer_failure':failed})
(out/'verdict.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results))
raise SystemExit(not all(r['passed'] for r in results))
