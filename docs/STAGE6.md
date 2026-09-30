# Macaros VM: stage-six acceptance

This is the execution checklist for John's VM goal. Architectural requirements
remain in `aros-next/docs/AARCH64.md`, `docs/ROADMAP.md` and the device-tree
feature specification. Passing an early boot probe does not complete a desktop
or prolonged-use gate. SMP belongs to bsek; do not develop a parallel version
unless a concrete blocker is documented.

| Stage | Acceptance | Current evidence / remaining work |
|---|---|---|
| 1. Interrupts and time | Kernel timer waits, scheduler mask semantics and standard IRQ handler registration/removal on the VM controllers | VZ and HVF GIC2/GIC3 pass. VZ block completion through resident MSI-X passes; other devices need their own IRQ qualification. Single CPU only. |
| 2. Portable hardware description | Retained firmware tree exposed through existing AROS APIs; shared bounded discovery; explicit mappings and driver admission | FDT reader, ACPI bridge, ECAM/windows and OpenFirmware API pass. PCI HIDD loading, registration/removal and MapPCI register reads pass in VZ/HVF. Resident bridge survives installer command unload in VZ/HVF; Owned-buffer DMA and polled virtio entropy completion pass in VZ/HVF; MSI frame discovery/mapping and MSI_TYPER read pass in VZ; resident vector ownership and block MSI-X completion pass. QEMU ITS routing remains. No Pi hardware qualification claimed. |
| 3. VZ desktop and input | Wanderer appears under Virtualization.framework and accepts keyboard/pointer input through guest drivers | Wanderer works under HVF/ramfb. VZ GPU GET_DISPLAY_INFO passes through shared PCI/DMA transport; surface creation, scanout, graphics HIDD and input remain. |
| 4. Persistent storage | Guest writes, flushes, remounts and verifies data after a full VM restart; controlled interruption recovery | Boot images and acknowledged virtio reset/start/stop probes pass in VZ/HVF. Shared virtio block transport/queue reads a boot sector in VZ/HVF. Shared MSI-X queue routing passes in VZ; full device startup and IRQ-backed CMD_READ pass in VZ. Isolated 4 KiB write/FLUSH/readback and fresh-VM persistence pass in VZ; Mounted FAT mkdir/write/rename/flush and fresh-VM file verification pass; One abrupt open-file write workload passes SIGKILL, read-only FAT consistency and fresh-VM durable-file verification; interruption coverage and sustained IO remain limited. |
| 5. Complete reproducible build | Empty build directory produces guest, SDK, commands and launcher; independent rebuild comparison | Fresh complete core and Wanderer packages boot. Packaging is deterministic. Two independent complete builds have not been compared byte for byte. |
| 6. Prolonged use and integration | Sustained desktop/storage/network exercise; clipboard text and images in both directions; usable file exchange | Network and integration services remain. An independently captured early-console TX timeout path is corrected and eight VZ boots pass; prolonged reliability still requires supervision. |

Next implementation boundary: VZ GPU surface/scanout and graphics HIDD, with
sustained filesystem IO and broader interruption coverage still required. Controller/bus startup failure
unwind is host-tested; published-bus hot removal remains unsupported. The transport uses MapPCI and bounded acknowledged
reset; block and mounted-FAT write/flush/restart gates pass; one process-cut recovery scenario passes; this is not exhaustive crash or host-power-loss qualification. Preserve access widths (especially command/status writes),
validate BARs against declared apertures, and do not enable DMA or assume INTx
without its platform contract. Reuse existing virtio transport where suitable;
VZ GPU/input/storage each need their own end-to-end acceptance evidence.

Keep run paths, exact hashes, decisions and limitations in
[`when john is back.md`](../when%20john%20is%20back.md). Review requests and
coordination live on the project board. Native Apple and Raspberry Pi proofs
remain distinct from VM and host-parser results.
