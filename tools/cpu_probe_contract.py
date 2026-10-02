# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Secondary execution and occupancy evidence; does not claim Exec SMP."""
import re

def cpu_probe_matches(text):
    if ('FAIL:' in text or text.count('[SMP] EL0 CPU anchor PASS')!=1
        or text.count('[SMP] CPU runtime isolation PASS')!=1
        or text.count('[SMP] IPI roundtrip PASS')!=1
        or text.count('[SMP] secondary execution probe started')!=1
        or text.count('[SMP] secondary execution probe stopped')!=1):
        return False
    pattern=r'\[SMP\] sample cpu=(0x[0-9a-f]+) affinity=(0x[0-9a-f]+) state=(0x[0-9a-f]+) busy=(0x[0-9a-f]+) idle=(0x[0-9a-f]+) transitions=(0x[0-9a-f]+) ipi_sent=(0x[0-9a-f]+) ipi_handled=(0x[0-9a-f]+) ipi_acked=(0x[0-9a-f]+)'
    rows=[tuple(int(v,16) for v in row) for row in re.findall(pattern,text)]
    if len(rows)<6 or len({r[:2] for r in rows})!=1: return False
    busy=idle=False
    for before,after in zip(rows,rows[1:]):
        if after[2] not in (1,2) or any(after[i]<before[i] for i in (3,4,5,6,7,8)): return False
        db=after[3]-before[3];di=after[4]-before[4]
        if not db+di: return False
        load=db*1000//(db+di)
        busy |= load>800
        idle |= load<200
    return busy and idle and rows[-1][5]>rows[0][5] and min(rows[-1][6:])>=3
