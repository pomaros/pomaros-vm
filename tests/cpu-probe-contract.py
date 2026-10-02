#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Reject incomplete, inconsistent and non-alternating CPU probe evidence."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from cpu_probe_contract import cpu_probe_matches

def sample(cpu, state, busy, idle, transitions):
    return "[SMP] sample " + " ".join(f"{k}=0x{v:x}" for k, v in zip(
        ("cpu", "affinity", "state", "busy", "idle", "transitions", "ipi_sent", "ipi_handled", "ipi_acked"),
        (cpu, 0x100, state, busy, idle, transitions, transitions, transitions, transitions))) + "\n"

start = "[SMP] EL0 CPU anchor PASS\n[SMP] CPU runtime isolation PASS\n[SMP] secondary execution probe started\n"
stop = "[SMP] secondary execution probe stopped\n[SMP] IPI roundtrip PASS\n"
rows = [sample(1, 1, 100, 0, 1), sample(1, 1, 200, 0, 1),
        sample(1, 2, 200, 100, 2), sample(1, 2, 200, 200, 2),
        sample(1, 1, 300, 200, 3), sample(1, 1, 400, 200, 3)]
good = start + "".join(rows) + stop
assert cpu_probe_matches(good)
for bad in (good.replace(stop, ""), start + good, good + "FAIL: late fault",
            start + "".join(rows[:3]) + stop,
            good.replace(rows[3], sample(2, 2, 200, 200, 2)),
            good.replace(rows[3], sample(1, 2, 100, 200, 2)),
            start + "".join(sample(1, 1, i * 100, 0, i) for i in range(6)) + stop):
    assert not cpu_probe_matches(bad), bad
print("CPU probe evidence: positive case and seven rejection cases PASS")
