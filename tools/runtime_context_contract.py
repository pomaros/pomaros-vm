# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exact CRT worker client result; startup/SMP acceptance is checked separately."""
PASS = 'CRT_ADMISSION PASS: selected cwd and descriptors survive pin instance switch'


def runtime_context_matches(text):
    rows = [line.strip() for line in text.splitlines() if 'CRT_ADMISSION' in line]
    return rows == [PASS]
