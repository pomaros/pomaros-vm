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

## Product packaging gates

| Command | Scope |
|---|---|
| `make test-host` | Host service framing, clipboard codec/services, personal disk safety and packaging admission; no VM execution. |
| `python3 tests/package-backend.py` | VZ host bridge defaults and QEMU persistent desktop without a host bridge; unsupported bridge requests rejected. |
| `python3 tests/configure-pin.py` | Source revision and guest x18 policy are admitted before configuration; mismatches create no build directory. |

| Command | Scope |
|---|---|
| `python3 tests/check-cpu-widget-frame.py --help` | Production 1024x768 UP CPUWidget: unique live accounting readiness, exact static frame/CPU label, bounded load text and contiguous gauge, 620 immutable-wallpaper blocks and eleven alert/missing-background negative controls. `--pixels-only` is an offline oracle check and never runtime acceptance. |

## CRT worker context

`tools/build-abi-probe --profile runtime-context --sdk /path/to/sdk/bin/apple-aarch64 OUTPUT`
builds the APL guest client in `tests/runtime-context.c` with debug information,
frame pointers and strict symbol resolution. Package it as `C/RuntimeContextProbe`
with that SDK's posixc/stdc/stdcio/fd libraries, then invoke it in an isolated
guest startup. It creates its own RAM directory, changes logical cwd, opens a
file and changes native stdin before pthread_create, forcing the code-pin open
to select another runtime instance. The worker checks both cwd and the retained
descriptor, then performs a relative open. Successful join and cleanup precede
the unique `CRT_ADMISSION PASS` marker on Exec's debug channel.

A successful compile/link or desktop boot does not pass this client: require
its exact marker once, no `CRT_ADMISSION FAIL`, and the normal guest verdict.
Keep the matching binary, source, runtime-library hashes and startup image.
This checks one worker and one instance switch; it does not establish arbitrary
creator removal, hot reload, all file-operation races or physical hardware.

The runtime-context client also guards native/CRT symbol collisions: POSIX
`open` and `close` must resolve through POSIXC, before the native Exec archive's
library lifecycle stubs of those names. A successful link alone misses this.
Optional `--debugger` prints section anchors and waits for LLDB; see
[the debugger procedure](../docs/BUILD.md). The ordinary invocation does not wait.
