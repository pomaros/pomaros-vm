#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Compare boot/WORK images from equivalent trees with different host metadata."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
fixtures={'EFI/BOOT/BOOTAA64.EFI':b'efi fixture\n',
          'AROS/GEN0.BND':bytes(range(256))*257,
          'AROS/BOOT0.ID':b'boot record\n','AROS/SESSION.CFG':b'forever\n',
          'AROS/nested/z.txt':b'last\n','AROS/nested/a.txt':b'first\n',
          'AROS/note-\u00e9.txt':b'UTF-8 name\n'}
verdict={'result':'FAIL','scope':'file-backed image creation; no guest boot','layouts':[]}
with tempfile.TemporaryDirectory(prefix='vm-disk-repro-') as directory:
    tmp=Path(directory)
    for side in range(2):
        stage=tmp/f'esp{side}'
        names=list(fixtures)
        if side:names.reverse()
        for name in names:
            path=stage/name;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(fixtures[name])
        for path in stage.rglob('*'):
            epoch=946684800 if side==0 else 1893456000
            os.utime(path,(epoch,epoch))
    for work in (False,True):
        images=[]
        for side in range(2):
            image=tmp/f'disk-{work}-{side}.img';images.append(image)
            subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(tmp/f'esp{side}'),
                            '--output',str(image)]+(['--work-volume'] if work else []),check=True,
                           env=dict(os.environ,TZ='Pacific/Honolulu' if side else 'Europe/Berlin'))
            for name,expected in fixtures.items():
                actual=subprocess.check_output(['mtype','-i',str(image),'::'+name],
                                               env=dict(os.environ,LC_ALL='en_US.UTF-8'))
                assert actual==expected,name
        hashes=[]
        for image in images:
            with image.open('rb') as f:hashes.append(hashlib.file_digest(f,'sha256').hexdigest())
        assert hashes[0]==hashes[1],(work,hashes)
        refusal=subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(tmp/'esp0'),
                                '--output',str(images[0])],capture_output=True)
        with images[0].open('rb') as f:after=hashlib.file_digest(f,'sha256').hexdigest()
        assert refusal.returncode!=0 and after==hashes[0]
        verdict['layouts'].append({'work':work,'sha256':hashes[0],
                                   'bytes':images[0].stat().st_size,'existing_output_unchanged':True})
    # Sized WORK: explicit flag, packaged provenance, and their disagreement.
    sized=tmp/'disk-512-explicit.img'
    subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(tmp/'esp0'),
                    '--output',str(sized),'--work-volume','--work-size','512'],check=True)
    assert sized.stat().st_size==(64+512)*1024*1024,sized.stat().st_size
    label=subprocess.check_output(['mdir','-i',f'{sized}@@{64*1024*1024}','::'],
                                  env=dict(os.environ,LC_ALL='en_US.UTF-8')).decode()
    assert 'WORK' in label,label
    (tmp/'esp0'/'provenance.json').write_text(json.dumps({'work_volume':True,'work_bytes':512*1024*1024}))
    packaged=tmp/'disk-512-packaged.img'
    subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(tmp/'esp0'),
                    '--output',str(packaged)],check=True)
    assert packaged.stat().st_size==sized.stat().st_size
    disagreement=subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(tmp/'esp0'),
                                 '--output',str(tmp/'disk-disagree.img'),'--work-volume','--work-size','256'],
                                capture_output=True)
    assert disagreement.returncode!=0 and not (tmp/'disk-disagree.img').exists()
    for bad in ('128','300','2048'):
        refused=subprocess.run(['python3',str(root/'tools/make-disk.py'),'--esp',str(tmp/'esp1'),
                                '--output',str(tmp/f'disk-bad-{bad}.img'),'--work-volume','--work-size',bad],
                               capture_output=True)
        assert refused.returncode!=0 and not (tmp/f'disk-bad-{bad}.img').exists(),bad
    (tmp/'esp0'/'provenance.json').unlink()
    verdict['layouts'].append({'work':True,'work_bytes':512*1024*1024,'bytes':sized.stat().st_size,
                               'packaged_size_honoured':True,'disagreement_refused':True})
verdict['result']='PASS'
print(json.dumps(verdict))
