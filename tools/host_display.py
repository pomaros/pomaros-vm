# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Keep an owned capture test visible without changing macOS power settings."""
from contextlib import contextmanager
import subprocess


@contextmanager
def keep_display_awake(process):
    # -u wakes an idle display; -d keeps it available for ScreenCaptureKit.
    # -w binds the assertion to this VM, even if its parent runner disappears.
    helper = subprocess.Popen(['/usr/bin/caffeinate', '-u', '-d', '-w', str(process.pid)],
                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
    try:
        yield
    finally:
        if helper.poll() is None:
            helper.terminate()
        try:
            helper.wait(timeout=3)
        except subprocess.TimeoutExpired:
            helper.kill()
            helper.wait()
