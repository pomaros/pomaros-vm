# Focused SMP checks

| Command | Scope |
|---|---|
| `python3 tests/dispatch-mask.py --core ../aros-core-smp` | Compiles the production dispatch-mask function with ASan/UBSan; checks readiness, Disable nesting and local exclusion. A mutation restoring the previous interrupt-depth policy must fail. |
| `python3 tests/exec-tls.py` | IRQ mask policy and AArch64 compilation of native and EL0 Exec TLS accessors. |
| `python3 tests/cpu-monitor.py` | Firmware topology and busy/idle accounting. |
| `python3 tests/exec-smp-contract.py` | Requires overlapping ordinary-task intervals on distinct CPUs in two phases, timer delivery and complete lifecycle; rejects 16 invalid transcripts. |
| `python3 tests/cpu-probe-contract.py` | Rejection of incomplete or inconsistent secondary-probe evidence. |

The dispatch policy check is separate from the guest Wait/delivery check:
Wait resumes with Disable nesting held and its final Enable may repair masks.
The secondary probe does not establish concurrent Exec scheduling.
