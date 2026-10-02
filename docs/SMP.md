# SMP integration

The Apple guest kernel is uniprocessor. `src/main.swift` assigns one VZ CPU;
increasing that count does not supply per-CPU Exec state or a scheduler.
The integration work follows native milestones A8/A9. Starting a secondary
CPU is a bring-up check, not A9 acceptance.

## Reuse from upstream

Bo Ståle Kopperud's merged [PR 1204](https://github.com/aros-development-team/AROS/pull/1204)
provides AArch64 atomics, SMP StackSwap, per-CPU state, spinlocks, CPU masks,
IPI work queues and scheduler/Exec integration. Merged
[PR 1318](https://github.com/aros-development-team/AROS/pull/1318)
extends the Raspberry Pi interrupt and CPU-release paths.

The portable atomics and StackSwap changes can be imported independently.
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
   constructs a tree from UEFI/ACPI but does not emit CPU nodes or a CPU-start
   interface: extend firmware discovery, including enabled MADT CPU entries
   and the firmware-advertised PSCI conduit, before publishing these bindings.
   Do not infer a conduit from the fact that the host is a Mac.
3. **Per-CPU kernel/Exec state.** Reuse upstream's TPIDR_EL1-based design:
   current task, nesting counters, quantum, interrupt readiness, idle task,
   exception stack and CPU identity. The Apple `kernel_cpu.c` and
   `dispatch_proof.c` use `SysBase->ThisTask`; `idle_active` and runtime
   interrupt readiness are global. Audit these consumers and exception paths
   together. Guest x18 remains an ordinary register.
4. **Secondary entry and interrupt transport.** Allocate separate stacks,
   install translation tables, cache attributes, vectors and per-CPU state
   before enabling interrupts. Split shared GIC distributor setup from local
   redistributor/CPU-interface setup; implement targeted SGIs and local timer
   setup. VZ requires its GICv3 path, not merely QEMU GICv2. For native M1,
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
Its result explicitly does **not** validate the AROS scheduler, memory-order
litmus properties, secondary CPU startup or native hardware.

Source hashes, binary hash and compiler/runtime logs accompany the verdict.
StackSwap still needs a guest build and execution check as part of the
kernel integration; this host test does not exercise it.
