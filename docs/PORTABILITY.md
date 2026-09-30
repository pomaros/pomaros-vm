# Shared AArch64 foundations

The architecture follows `aros-next/docs/AARCH64.md`, its
`tracks/aarch64-generic.md`, `features/device-tree/spec.md` and `ROADMAP.md`.
The order is early FDT access, core interrupt/timer/console bindings, then
virtio devices and desktop integration. The private VM branch supplies reusable
components and evidence for that sequence; publishing upstream is a separate
explicit action.

| Boundary | Contract |
| --- | --- |
| Firmware adapter | Preserve a supplied FDT. Translate VZ's ACPI description into an FDT at the loader boundary. |
| Early reader | Shared allocator-free libfdt adapter, immutable tree, bounded input, paths/aliases/cells/ranges/interrupt suppliers. |
| Platform binding | Match compatible strings. Register windows and IRQ descriptions come from the tree. |
| Interrupt controller | GICv2/GICv3 binding owns controller-specific encoding; shared timer code sees a resolved interrupt. AIC is a separate adapter. |
| Timer | ARM architectural timer mechanism, with an explicitly selected physical or virtual channel. The probe uses CNTV because CNTP_TVAL is unavailable under QEMU/HVF. |
| Runtime discovery | The retained tree is handed to the existing `openfirmware.resource` path; later drivers belong there. |
| DMA | Device drivers require a bus-address mapping contract. Identity DMA in a VM does not establish a Pi bus-offset or Apple DART contract. |
| ABI | VM guest x18 is unrestricted; macOS reserves host x18. Physical Apple migration of its existing platform-register ABI is a separately qualified change. |

The GIC component is a single-CPU bootstrap owner. Its operations cover delivery,
masking, acknowledgement, EOI and a self-SGI. SMP admission and cross-CPU routing
need a topology-aware interface before secondary CPU startup. Device tree
schema validation, runtime device identity/hotplug and resource grants retain
their own roadmap gates.

The portable guest tests the reader and controller code directly, before an OS
scheduler. Its `[DT0]`/`[DT2]` observations are scoped to that probe. They do not
close the roadmap's full-platform gates, prove physical Pi hardware, or migrate
the Apple kernel onto a shared IRQ implementation. Full AROS startup is measured
separately on VZ and HVF. Exact revisions, results and next work are in
[`when john is back.md`](../when%20john%20is%20back.md).
