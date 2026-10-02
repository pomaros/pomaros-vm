# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded ordinary Exec tasks; does not qualify drivers or networking."""
import re

MARKERS = (
    '[KI] alive window open',
    '[SMP] EL0 CPU anchor PASS',
    '[SMP] ordinary task concurrent compute + x18/FPSIMD PASS',
    '[SMP] ordinary task wake + concurrent compute PASS',
    '[SMP] ordinary task teardown PASS',
    '[SMP] Exec two-core tasks PASS',
    '[SMP] load widget ready',
    '[SMP] Exec secondary stopped PASS',
)
WORKER = re.compile(
    r'\[SMP\] task cpu=(0x[0-9a-f]+) phase=(0x[0-9a-f]+) '
    r'start=(0x[0-9a-f]+) end=(0x[0-9a-f]+) iterations=(0x[0-9a-f]+)')
COMPUTE_TIMER = re.compile(
    r'\[SMP\] AP compute timer ticks phase=(0x[0-9a-f]+) ticks=(0x[0-9a-f]+)')


def exec_smp_matches(text, cpus):
    if cpus < 2 or 'FAIL' in text or any(text.count(m) != 1 for m in MARKERS):
        return False
    rows = [tuple(int(value, 16) for value in row) for row in WORKER.findall(text)]
    if len(rows) != 4:
        return False
    ids = None
    for phase in (0, 1):
        pair = [row for row in rows if row[1] == phase]
        if len(pair) != 2:
            return False
        if any(cpu >= cpus or end <= start or not iterations
               for cpu, _, start, end, iterations in pair):
            return False
        current = {row[0] for row in pair}
        if len(current) != 2 or (ids is not None and ids != current):
            return False
        ids = current
        if min(row[3] for row in pair) <= max(row[2] for row in pair):
            return False

    # Idle-only timer delivery cannot prove interrupt safety under computation.
    during = [(int(phase, 16), int(count, 16))
              for phase, count in COMPUTE_TIMER.findall(text)]
    if len(during) != 2 or {phase for phase, _ in during} != {0, 1}:
        return False
    if any(count < 2 for _, count in during):
        return False
    ticks = re.findall(r'\[SMP\] AP timer ticks=(0x[0-9a-f]+)', text)
    return len(ticks) == 1 and int(ticks[0], 16) >= 2
