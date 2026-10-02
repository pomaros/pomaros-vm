# SMP integration

Exec task scheduling is uniprocessor. The launcher defaults to one VZ CPU;
`--cpus=N` changes the virtual hardware inventory, not Exec scheduling.
An explicit secondary execution probe can run on a second CPU without
accessing Exec lists or drivers.
The integration work follows native milestones A8/A9. Starting a secondary
CPU is a bring-up check, not A9 acceptance.

## Experimental ordinary-task build

`tools/configure-core.py --smp --source ../aros-core-smp --output PATH`
selects the isolated `codex-smp-integration` branch and a fresh SMP SDK.
SMP changes ExecBase's ABI; objects and programs from the UP SDK must not be
mixed into that image. `package-core.py --smp-exec` selects its bounded task
proof, and `run-qemu.py --cpus 2 --require-exec-smp --console virtio
--virtio-desktop` requires its evidence. This experimental configuration is
separate from the qualified UP secondary-probe image.

The backend shares upstream scheduler, affinity and spinlock mechanisms.
Exec and kernel are separate boot modules. A typed callback table published
through the CPU anchor before Exec initialization carries private lock, IRQ
exclusion and IPI operations. The kernel owns the shared queues and state;
Exec forwarding stubs introduce no unresolved boot-module imports.

VM-specific code admits one PSCI secondary with its own EL1 stack, CPU anchor,
GIC redistributor and timer. SGIs deliver scheduler notifications and queued
Exec signal hooks. Firmware affinity selects the destination; logical IDs index
per-CPU records. EL0 task code keeps x18 available to the compiler.

Desktop children inherit boot-CPU affinity; explicit compute workers use the
secondary. Exec's shared housekeeper retains its upstream affinity policy.
Two task phases exercise concurrent calculation, Wait/Signal, register state
and teardown. Host validation requires overlapping counter intervals on two
distinct CPUs, AP timer delivery and confirmed PSCI CPU_OFF before unloading
the kernel. This bounded test does not qualify task migration, network or
native Apple drivers for SMP.

## Reuse from upstream

Bo Ståle Kopperud's merged [PR 1204](https://github.com/aros-development-team/AROS/pull/1204)
provides AArch64 atomics, SMP StackSwap, per-CPU state, spinlocks, CPU masks,
IPI work queues and scheduler/Exec integration. Merged
[PR 1318](https://github.com/aros-development-team/AROS/pull/1318)
extends the Raspberry Pi interrupt and CPU-release paths.

The integration branch imports the upstream SMP kernel/Exec layer with its
original authorship, as well as the portable atomics and StackSwap changes.
The scheduler implementation lives in `arch/aarch64-native`, whereas this
guest builds `arch/aarch64-apple`. Importing those files alone does not wire
them into this target. Reuse the generic mechanisms through shared AArch64
code; retain separate GIC and Apple AIC/CPU-release implementations. The Pi
register setup must not become an Apple backend.

## Activation sequence

1. **Common prerequisites.** Import the real atomic operations and StackSwap
   task lookup; build both UP and SMP configurations. Check lock ordering and
   memory barriers before connecting Exec lists to concurrent CPUs.
2. **CPU description.** Consume enabled CPU nodes and MPIDR affinities from
   FDT. Preserve logical CPU IDs separately from hardware affinity values.
   QEMU supplies firmware CPU/PSCI data. VZ's `boot/arosboot/vz_platform.c`
   constructs CPU nodes from enabled MADT GICC entries and publishes the PSCI
   conduit advertised by FADT ARM_BOOT_ARCH. The parser rejects duplicate
   affinities, malformed entries, absent PSCI support and excess CPUs.
   Do not infer a conduit from the fact that the host is a Mac.
3. **Per-CPU kernel/Exec state.** Adapt upstream's per-CPU design to an EL0-readable TPIDRRO_EL0 anchor:
   current task, nesting counters, quantum, interrupt readiness, idle task,
   exception stack and CPU identity. Runtime privilege transitions, interrupt
   readiness/depth, observation continuations and idle state are CPU-local.
   The Apple dispatch paths use the Exec platform accessors, selecting
   SysBase storage for UP and CPU-local TLS for SMP. Observation records use
   this CPU anchor on the boot and secondary CPUs; it
   is never saved or restored as task state. Guest x18 remains an ordinary register.
   `AROSAppleRuntimeIRQSave/IRQRestore` provide nested CPU-local IRQ/FIQ
   exclusion from EL0 through SVC, and from early EL1 directly. Restore changes
   only those two mask bits and respects Disable nesting, interrupt depth and
   controller readiness. A CPU-local depth preserves exclusion across return
   paths. Returning from an idle interrupt reapplies the destination task’s
   mask policy after leaving interrupt context; the task must not inherit the
   handler’s temporary interrupt depth. Scheduler syscalls are rejected while the depth is nonzero; save/restore
   pairs must stay on the same CPU, in nesting order, without blocking.
   Masking FIQ also masks the native Apple timer: an unbounded critical section
   can stall its CPU. Per-CPU entry/rejection counts, start tick and longest
   completed interval support debugger inspection; they are not a watchdog.
   The first rejected scheduler call is logged. Returning to the bootstrap or
   abandoning an exception continuation diagnoses and clears an unfinished
   exclusion interval. An unbound Exec TLS access traps rather than proceeding
   with a null CPU anchor.
   This does not exclude other CPUs; shared scheduler lists also require spinlocks.
   The Apple `exec_platform_smp.h` adapter reuses upstream Exec accessors and
   TLS layout through `TLS_PTR_GET`, mapping local exclusions to these runtime
   calls. Each CPU anchor points to separate Exec TLS storage. The adapter is
   not selected by the UP build. The experimental SMP configuration selects
   it together with the scheduler and live task attachment. Native privileged
   access retains TPIDR_EL1.
4. **Secondary entry and interrupt transport.** Allocate separate stacks,
   install translation tables, cache attributes, vectors and per-CPU state
   before enabling interrupts. The shared GIC separates one-time distributor setup from local
   redistributor/CPU-interface setup and targets GICv3 SGIs by full affinity.
   Local timer setup and scheduler wakeups must use these per-CPU interfaces. VZ requires its GICv3 path, not merely QEMU GICv2. For native M1,
   coordinate Apple CPU release and AIC/IPI handling independently; removing
   m1n1's `GUEST_NOSMP` alone is not a kernel implementation.
5. **Scheduler and shared resources.** Connect upstream CPU masks, IPI queues,
   wakeups, rescheduling and task teardown. Audit Apple page allocation,
   page-table modification/TLB invalidation, interrupt registration and driver
   queues for cross-CPU exclusion. `Forbid()` or local IRQ masking alone does
   not establish that exclusion. Migrate scheduler state with FPSIMD context.
6. **Bounded activation.** Use an isolated two-CPU image first: prove distinct
   CPU identities, IPI acknowledgements both ways, local ticks, task affinity,
   migration and concurrent create/signal/exit. Require a deadline and strict
   verdict; keep UP boot as a regression check. Then test VZ and finally the
   M1 with Claude coordinating target ownership. The native A9 milestone also
   requires all expected cores and its separate sustained stress acceptance.

## Prerequisite check

```sh
python3 tests/smp-atomics.py --core ../aros-core-smp \
  --output artifacts/smp-atomics-check
```

This checks the actual production header selection, compiles twelve primitive
functions for AArch64 ELF, and runs four macOS threads contending on byte,
word and long counters/masks. It checks wraparound and adjacent guards.
The host test substitutes only AROS scalar typedefs, not the atomic macros.
The bitwise phase checks each worker's bit between synchronized OR/AND phases;
a deliberately broken load/store RMW negative control must fail. ELF
disassembly must contain an exclusive loop and two fences in every primitive.
Its result explicitly does **not** validate the AROS scheduler, memory-order
litmus properties, secondary CPU startup or native hardware.

Source hashes, binary hash and compiler/runtime logs accompany the verdict.
StackSwap still needs a guest build and execution check as part of the
kernel integration; this host test does not exercise it.


## Per-core observation

The AROS alive widget lists CPU IDs from firmware, with separate `OFF`,
`DISABLED`, `BOOT`, `RUN`, `TEST` and unavailable states. The heading gives
the active/firmware CPU count. Each row has an aligned percentage and load
bar; `TEST` identifies a secondary running the isolated probe. CPU IDs are logical indices;
complete MPIDR affinities are retained separately. If firmware discovery fails,
only the observed boot CPU is listed and the heading says firmware unknown.
Eight rows are displayed at a time; larger inventories rotate pages every five
seconds. A percentage is computed from busy/idle counter deltas between
samples, not from total uptime or the number of configured virtual CPUs.

The executing CPU owns its accounting record. Exception entry counts as busy;
the scheduler's WFI entry counts as idle. Readers make bounded snapshot
attempts and show unavailable on contention. Offline CPUs have no invented
zero-percent load. This measures execution versus idle residence, not host
CPU use, power consumption or scheduler correctness.

## Isolated secondary execution probe

`tools/package-core.py --smp-probe --desktop-session 12` writes
`AROS/CPUPROBE.CFG`. The EFI ACPI adapter publishes the explicit opt-in in
`/chosen`. Expose at least two CPUs with `--cpus=2` in the launcher or
qualification runner. The ordinary desktop does not opt in.

The boot CPU starts one firmware-enabled secondary through PSCI CPU_ON.
Its EL1 entry installs the identity translation regime and a private stack
and exception-vector table. It confirms its MPIDR and independent runtime
state, alternates busy execution
and WFE periods on boot-CPU commands, and publishes its own counters. It does
not call Exec, process device interrupts, or run AROS applications. It handles
reserved SGI 14 through a private IRQ entry and sends SGI 15 back to the boot
CPU. The IRQ frame preserves every general register, including x18. Startup
of another GIC CPU interface does not reset the shared distributor or disable
boot-CPU device interrupts. At shutdown the EL0 hold task allows up to one second for outstanding IPI
replies to arrive before entering the IRQ-masked stop supervisor. The probe
requires at least three requests and exactly matching handled/acknowledged
counts. A drain timeout fails qualification but still attempts CPU_OFF. Its widget
row says `TEST`, never scheduler-ready. The secondary must acknowledge PSCI
CPU_OFF through AFFINITY_INFO before the bounded session releases the kernel;
a failure retains the kernel rather than returning through reclaimed code.

`tools/run-vz.py` and `tools/run-qemu.py` accept `--require-smp-probe` and
require monotonic counters, both high and low occupancy intervals, startup
confirmed stop and bidirectional IPI completion, alongside their normal guest
boot checks. Their verdict
keeps `exec_smp_verified` false. QEMU failures preserve registers for every
configured CPU. This probe exercises CPU activation and the widget's source
of measurements; it does not replace task migration, IPI and driver testing.

Host checks: `python3 tests/exec-tls.py` (IRQ policy and both Exec accessor
backends, including idle-to-task mask restoration), `python3 tests/cpu-monitor.py` and
`make build/vz-platform-test && build/vz-platform-test`.

The guest timer proof wakes from idle and then spins without making a system
call until another timer request completes. `[KI] idle wake compute PASS`
checks that timer delivery survives the return to a compute-bound EL0 task.

AROSTCP SMP safety is a separate acceptance requirement. Its unpublished
upstream fixes must be integrated and tested before claiming concurrent
network operation; a secondary execution probe provides no such evidence.

## Running the bounded CPU probe

Use the isolated `codex-smp-integration` core and its configured build. Build
`apple-wanderer-payloads`, `kernel-hidd-pci`, `kernel-hidd-vmpci`,
`kernel-hidd-virtiogfx` and `workbench-c` with their dependencies. Build the
EFI adapter against the same source. Example:

```sh
python3 tools/package-core.py --build "$HOME/Build/macaros-vm/smp-core" \
  --efi build/uefi-smp/BOOTAA64.EFI --profile wanderer --virtio-gpu \
  --desktop-session 12 --smp-probe --output build/cpu-probe
python3 tools/run-qemu.py --esp build/cpu-probe --console virtio \
  --virtio-desktop --msi-controller gicv2m --cpus 2 --require-smp-probe \
  --require-timer --require-gpu-irq --require-irq-chain \
  --output artifacts/cpu-probe-hvf
python3 tools/make-disk.py --esp build/cpu-probe --output build/cpu-probe.img
python3 tools/run-vz.py --disk build/cpu-probe.img --desktop-startup \
  --require-desktop-session 12 --cpus 2 --require-smp-probe --require-gpu-irq \
  --output artifacts/cpu-probe-vz
```

The CPU probe protocol captures `cpu-widget.png` during the live QEMU session.
It does not apply the empty-desktop pixel classifier to a session containing a
shell and widget. `pixels_verified` and `desktop_checked` remain false; the
image is retained for visual review. The ordinary QEMU desktop qualification
keeps its existing pixel checks. Host evidence-parser rejection cases run with
`python3 tests/cpu-probe-contract.py`.

GICv3 IPI targeting currently admits Aff0 values 0..15; larger range-selector
values fail explicitly. GICv2 self-IPI remains available, but cross-CPU target
bits are not inferred from MPIDR. Apple AIC/IPI and native spin-table release
are separate backends and are not qualified by these VM tests.
