# Shared AArch64 foundations

The architecture follows `aros-next/docs/AARCH64.md`, its
`tracks/aarch64-generic.md`, `features/device-tree/spec.md` and `ROADMAP.md`.
The order is early FDT access, core interrupt/timer/console bindings, then
virtio devices and desktop integration. The public common core supplies reusable components for that sequence.
The historical VM branch supplies earlier evidence; submission to official
AROS upstream remains a separate reviewed action.

| Boundary | Contract |
| --- | --- |
| Firmware adapter | Preserve a supplied FDT. Translate VZ's ACPI description into an FDT at the loader boundary. |
| Early reader | Shared allocator-free libfdt adapter, immutable tree, bounded input, paths/aliases/cells/ranges/interrupt suppliers. |
| Platform binding | Match compatible strings. Register windows and IRQ descriptions come from the tree. |
| Interrupt controller | GICv2/GICv3 binding owns controller-specific encoding; shared timer code sees a resolved interrupt. AIC is a separate adapter. |
| Timer | ARM architectural timer mechanism, with an explicitly selected physical or virtual channel. The probe uses CNTV because CNTP_TVAL is unavailable under QEMU/HVF. |
| Runtime discovery | The retained tree is handed to the existing `openfirmware.resource` path; later drivers belong there. |
| DMA | Device drivers require a bus-address mapping contract. Identity DMA in a VM does not establish a Pi bus-offset or Apple DART contract. |
| ABI | VM guest x18 is unrestricted; macOS reserves host x18. Native Apple and future Raspberry Pi use the same ordinary register policy; their hardware qualification remains separate. |

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
[qualification status](STATUS.md).

The VZ loader translates a checksummed ACPI MADT GICv3 distributor and one
redistributor range, plus GTDT non-secure physical and virtual timer GSIs,
into standard GIC/timer nodes. It emits `interrupt-names = "phys", "virt"`;
the kernel resolves `virt` by name, falling back to binding index 2 only when
names are absent. No secure timer IRQ is invented when ACPI omits it.
Unsupported controller versions, multiple redistributor ranges, overlapping
windows, non-PPI timer GSIs and unsupported polarity fail discovery.

The ACPI reader trusts firmware-provided physical table pointers. It validates
checksums, declared table sizes and record boundaries; it is not a parser for
arbitrary pointers supplied by applications. CPU enumeration, ACPI resource
AML and general PCI routing belong to subsequent platform work.

Field meanings follow [ACPI 6.6, MADT and GTDT](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html).
Timer names follow the [architectural timer binding](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/timer/arm%2Carch_timer.yaml);
only the binding facts were used, with no GPL implementation copied.

VM device handlers use the existing `KrnModifyIRQA`, `KrnAddIRQHandler` and
`KrnRemIRQHandler` interfaces. A driver first configures an SPI with explicit
edge/level and active-high attributes. Unconfigured sources, kernel PPIs and
reconfiguration with installed handlers are refused. Dispatch visits the shared
AROS callback chain; removing its last handler disables the GIC source. This
adapter is serialized with `Disable()` and is qualified only on one CPU.

The boot qualification injects a pending SPI through the distributor. It checks
two callbacks, removal of one, and hardware masking after the last removal.
It does not simulate a peripheral's acknowledgement protocol or establish DMA
correctness. SGIs are unsuitable for the masking assertion on GICv2: their
enable/disable behavior is implementation-defined ([Arm IHI 0048B, GICD_ICENABLER](https://www.scs.stanford.edu/~zyedidia/docs/arm/gic.pdf)).

The VZ firmware adapter obtains PCI memory apertures through UEFI root bridge
`Configuration()` before ExitBootServices. It emits a `pci-host-ecam-generic`
node with ECAM, bus range and PCI memory `ranges`; it accepts one segment-zero
root with bus zero and identity memory translations. Unsupported translations,
malformed descriptors and overlapping apertures are rejected. I/O port
apertures are omitted because this profile has no port-I/O consumer. DMA and
MSI routing require separate bindings; a PCI description alone grants neither.

The resource descriptor format follows [UEFI PCI bus support](https://uefi.org/specs/UEFI/2.10/14_Protocols_PCI_Bus_Support.html).
Protocol ABI offsets were cross-checked against [EDK II declarations](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/PciRootBridgeIo.h);
no driver implementation was imported. Firmware-owned buffers have no supplied
length: their bounded descriptor walk has the same trusted-pointer boundary as
ACPI tables. The parser tests use explicit byte lengths and sanitizer checks.

The shared `pci_ecam` early binding validates one direct-root ECAM host,
3/2 PCI address/size cells, 2/2 CPU cells, bus bounds, configuration coverage,
and at most eight ranges. It preserves PCI-to-CPU memory translations.
Stage two maps ECAM as non-posted Device memory and memory apertures as posted
Device memory, using the existing MMU builder. RAM overlaps and intersections
with other controllers fail the map contract. A console fully contained in a
PCI window shares that mapping. Port I/O has no mapping in this profile.
These mappings provide reachability; runtime driver admission and DMA ownership
are separate contracts.

`openfirmware.resource` now materializes its existing node/property representation
from `KRN_FlattenedDeviceTree` when no legacy `KRN_OpenFirmwareTree` is supplied.
It uses the shared bounded reader and borrows names/values from the retained
immutable blob; only metadata is allocated. Every allocation failure unwinds the
whole partial tree. The legacy Pi boot-tree path remains available. This is
architecture-neutral C; the acceptance runs cover VM guests, not a Pi boot.

The runtime proof opens the resource, searches a compatible PCI node through its
public vectors and reads vendor IDs from the mapped ECAM in supervisor mode.
It does not register a PCI HIDD, allocate BARs, enable bus mastering or establish
DMA/interrupt ownership. Those are the next driver integration boundaries.
