#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Check the real core make date definitions, including GNU make shell handling."""
from pathlib import Path
import os
import subprocess
import tempfile

core=Path(__file__).resolve().parents[2]/'aros-core-vm'
source=(core/'config/make.cfg.in').read_text()
block=source[source.index('# Reproducible command version'):source.index('# Bootstrap ELF')]
count=0
with tempfile.TemporaryDirectory(prefix='aros-build-date-') as directory:
    makefile=Path(directory)/'Makefile'
    for epoch,expected in [('0','01.01.1970 1970-01-01'),('951782400','29.02.2000 2000-02-29'),('1893456000','01.01.2030 2030-01-01')]:
        makefile.write_text('PYTHON := python3\nSRCDIR := '+str(core)+'\nSOURCE_DATE_EPOCH := '+epoch+'\n'+block+
                            '\nall:\n\t@echo $(AROS_BUILD_DATE) $(AROS_BUILD_ISODATE)\n\t@printenv SOURCE_DATE_EPOCH\n')
        for zone in ['Pacific/Honolulu','Europe/Berlin']:
            env=dict(os.environ,TZ=zone);env.pop('SOURCE_DATE_EPOCH',None)
            result=subprocess.run(['make','-f',str(makefile)],capture_output=True,text=True,env=env)
            assert result.returncode==0 and result.stdout.splitlines()==[expected,epoch],result
            count+=1
    for value in ['', '-1', '1;false', '999999999999999999999999']:
        result=subprocess.run(['make','-f',str(makefile),'SOURCE_DATE_EPOCH='+value],capture_output=True,text=True)
        assert result.returncode!=0,result
        count+=1
print(f'build-date result=PASS cases={count} make-propagation,UTC,leap-day,epoch-zero,invalid-refusal')
