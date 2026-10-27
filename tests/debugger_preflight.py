# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""LLDB command: verify a paused QEMU GDB connection before a workload."""
import argparse
import json
from pathlib import Path
import shlex
import time
import lldb


def qualify(debugger, command, result, internal_dict):
    parser = argparse.ArgumentParser(prog='vm-debugger-preflight')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cpus', type=int, default=2)
    args = parser.parse_args(shlex.split(command))
    if args.output.exists():
        result.SetError("output must be a new path")
        return
    process = debugger.GetSelectedTarget().GetProcess()
    was_async = debugger.GetAsync()
    try:
        assert process.IsValid() and process.GetState() == lldb.eStateStopped
        assert process.GetNumThreads() == args.cpus
        thread = process.GetThreadAtIndex(0)
        before = thread.GetFrameAtIndex(0).FindRegister('pc').GetValueAsUnsigned()
        error = lldb.SBError()
        memory = process.ReadMemory(before, 16, error)
        assert error.Success() and len(memory) == 16, str(error)
        debugger.SetAsync(False)
        thread.StepInstruction(False)
        assert process.GetState() == lldb.eStateStopped
        stepped = thread.GetFrameAtIndex(0).FindRegister('pc').GetValueAsUnsigned()
        assert stepped != before
        debugger.SetAsync(True)
        error = process.Continue()
        assert error.Success(), str(error)
        time.sleep(.05)
        error = process.Stop()
        assert error.Success(), str(error)
        until = time.monotonic() + 3
        while process.GetState() != lldb.eStateStopped and time.monotonic() < until:
            time.sleep(.01)
        assert process.GetState() == lldb.eStateStopped
        threads = []
        for thread in process:
            frame = thread.GetFrameAtIndex(0)
            threads.append(dict(id=thread.GetThreadID(),
                pc=frame.FindRegister('pc').GetValueAsUnsigned(),
                sp=frame.FindRegister('sp').GetValueAsUnsigned()))
        evidence = dict(result='PASS', threads=threads, initial_pc=before,
            stepped_pc=stepped, initial_memory_hex=bytes(memory).hex(),
            resume_interrupt_verified=True,
            scope='QEMU GDB transport before the workload; no kernel or hardware acceptance')
        args.output.write_text(json.dumps(evidence, indent=2) + '\n')
        result.AppendMessage('Debugger preflight PASS: registers, memory, step, resume/interrupt')
    except Exception as error:
        args.output.write_text(json.dumps(dict(result='FAIL', error=str(error) or type(error).__name__), indent=2) + '\n')
        result.SetError(str(error) or type(error).__name__)
    finally:
        debugger.SetAsync(was_async)
