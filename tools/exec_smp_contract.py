# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded ordinary Exec tasks; does not qualify drivers or networking."""
import re
MARKERS = (
    '[SMP] EL0 CPU anchor PASS',
    '[SMP] ordinary task concurrent compute + x18/FPSIMD PASS',
    '[SMP] ordinary task wake + concurrent compute PASS',
    '[SMP] ordinary task teardown PASS',
    '[SMP] Exec two-core tasks PASS',
    '[SMP] Exec secondary stopped PASS',
)
def exec_smp_matches(text, cpus):
    if cpus < 2 or 'FAIL' in text or any(text.count(m)!=1 for m in MARKERS):
        return False
    rows = [tuple(int(x,16) for x in row) for row in re.findall(
        r'\[SMP\] task cpu=(0x[0-9a-f]+) phase=(0x[0-9a-f]+) start=(0x[0-9a-f]+) end=(0x[0-9a-f]+) iterations=(0x[0-9a-f]+)',text)]
    if len(rows)!=4: return False
    ids=None
    for phase in (0,1):
        pair=[r for r in rows if r[1]==phase]
        if len(pair)!=2 or any(r[0]>=cpus or r[3]<=r[2] or not r[4] for r in pair): return False
        current={r[0] for r in pair}
        if len(current)!=2 or (ids is not None and ids!=current): return False
        ids=current
        if min(r[3] for r in pair)<=max(r[2] for r in pair): return False
    ticks=re.findall(r'\[SMP\] AP timer ticks=(0x[0-9a-f]+)',text)
    return len(ticks)==1 and int(ticks[0],16)>=2
