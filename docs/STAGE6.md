# Macaros VM: stage-six acceptance

This is the execution checklist for John's VM goal. Architectural requirements
remain in `aros-next/docs/AARCH64.md`, `docs/ROADMAP.md` and the device-tree
feature specification. Passing an early boot probe does not complete a desktop
or prolonged-use gate. SMP belongs to bsek; do not develop a parallel version
unless a concrete blocker is documented.

| Stage | Acceptance | Current evidence / remaining work |
|---|---|---|
| 1. Interrupts and time | Kernel timer waits, scheduler mask semantics and standard IRQ handler registration/removal on the VM controllers | VZ and HVF GIC2/GIC3 pass. VZ block completion through resident MSI-X and network interrupt delivery during DHCP and resident vsock IO pass; other devices need their own IRQ qualification. Single CPU only. |
| 2. Portable hardware description | Retained firmware tree exposed through existing AROS APIs; shared bounded discovery; explicit mappings and driver admission | FDT reader, ACPI bridge, ECAM/windows and OpenFirmware API pass. PCI HIDD loading, registration/removal and MapPCI register reads pass in VZ/HVF. Resident bridge survives installer command unload in VZ/HVF; Owned-buffer DMA and polled virtio entropy completion pass in VZ/HVF; MSI frame discovery/mapping and MSI_TYPER read pass in VZ; resident vector ownership and block MSI-X completion pass. QEMU ITS routing remains. No Pi hardware qualification claimed. |
| 3. VZ desktop and input | Wanderer appears under Virtualization.framework and accepts keyboard/pointer input through guest drivers | Wanderer renders verified pixels through the resident virtio graphics HIDD under HVF (EFI regression also passes). The same desktop guest passes startup/foreground checks under VZ. VZ pixel capture remains unverified; GPU completion is polled. VZ input exposes Apple PCI xHCI (not virtio-input); capability mapping and actual driver OpenDevice/query pass after removing the legacy INTx-line admission restriction. Poseidon enumerates the root hub and reads two child descriptors under VZ after PCI-owned payload integration and observed-bit PORTSC acknowledgment. The desktop SYS startup loads Poseidon/HID and two configured child HID bindings pass with Wanderer startup. A bounded 30-second desktop session with shell, 15 ticks and completed startup passes under VZ. A targeted VZ-view event sequence reaches an Intuition window through USB/HID (key down/up, button down/up, two coordinate-checked moves and native-width tablet button attributes). Physical input, prolonged use and cancellation/teardown lifetime remain unqualified. |
| 4. Persistent storage | Guest writes, flushes, remounts and verifies data after a full VM restart; controlled interruption recovery | Boot images and acknowledged virtio reset/start/stop probes pass in VZ/HVF. Shared virtio block transport/queue reads a boot sector in VZ/HVF. Shared MSI-X queue routing passes in VZ; full device startup and IRQ-backed CMD_READ pass in VZ. Isolated 4 KiB write/FLUSH/readback and fresh-VM persistence pass in VZ; Mounted FAT mkdir/write/rename/flush and fresh-VM file verification pass; One abrupt open-file write workload passes SIGKILL, read-only FAT consistency and fresh-VM durable-file verification; interruption coverage and sustained IO remain limited. Optional desktop WORK: auto-mount now uses standard Mount and virtio.device. The desktop path now passes 64 KiB write, FAT drain, negotiated virtio FLUSH and fresh-VM read with independent host bytes, alongside USB/DHCP/DNS/clipboard. Device/volume naming and startup publication races were corrected. Host-requested shutdown now waits for VMSync to inhibit and flush VMWORK0 before stopping; a bounded SIGTERM/guest-ack/host-byte check passes. GUI close shares that path but has not been independently clicked. Unsaved application buffers and other volumes are outside this contract. Embedded SYS is RAM-backed. |
| 5. Complete reproducible build | Empty build directory produces guest, SDK, commands and launcher; independent rebuild comparison | Fresh complete core and Wanderer packages boot. Packaging is deterministic. Two independent builds of frozen core 5be666ebbe complete all eight desktop/PCI/virtio/USB targets: all 18,069 product/SDK files and stage2 (included in that count) match byte for byte. Current launcher (whole-module Swift compilation) and EFI now match in independent source/build directories. Outer boot/WORK FAT images and signed app files match independent packaging runs. Later guest network/vsock/device additions and additional base commands still need the updated full guest comparison. |
| 6. Prolonged use and integration | Sustained desktop/storage/network exercise; clipboard text and images in both directions; usable file exchange | Raw virtio-net TX/RX with a transaction-checked DHCP discover/offer passes in VZ NAT. Resident SANA-II OpenDevice/query/configuration/read cancellation/close/reopen passes in VZ. AROSTCP obtains a DHCP lease through resident SANA-II in VZ NAT. An AROSTCP UDP round trip with an independently checked 32-byte host challenge passes via the Mac NAT bridge address. Earlier attempts through the Mac Wi-Fi address failed; their cause remains unproven. A bounded 32-byte vsock stream round trip passes between AROS and the VZ launcher. The resident vmbridge.device passes two 64 KiB Exec IO cycles with pending-read abort, write drain, IRQ completions and close/reopen. Host file-service socket-pair tests and the VZ gate pass: upload/readback of 64 KiB, no-replace/path refusal, and standalone VMExchange HELLO with reopen. A focused QEMU gate also verifies nested command startup. CLI PUT/GET, existing-destination refusal and guest-file persistence across two fresh VZ VMs pass with independent host byte checks. Desktop integration remains. The host clipboard endpoint passes named-pasteboard/socket tests. Two fresh VZ boots pass real clipboard.device text/image synchronization, host-wins conflicts, unchanged revisions and normal session reconnects; standalone VMSync ONCE imports correctly. A bounded 20-second VZ Wanderer session verifies the actual auto-started daemon through a separate clipboard-device client: text/images, foreground session contention/recovery, independently loaded VMExchange HELLO and no echo. GUI editing commands, forced transport failure recovery and prolonged reliability remain unqualified. The combined 20-second desktop session passes AROSTCP auto-start, DHCP and DNS resolution through the DHCP-provided resolver alongside clipboard and USB/HID. Missing T: caused the earlier DHCP-client requester; normal MakeDir/Assign startup fixes it without relaxing desktop acceptance. TCP/HTTP and sustained network usage remain unqualified. An independently captured early-console TX timeout path is corrected and eight VZ boots pass; prolonged reliability still requires supervision. |

Next implementation boundary: VZ desktop visual/input acceptance and GPU IRQ completion, with
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

VZ network configuration admission passes (guest MAC equals persisted host MAC).
The separate `--require-network-packets` gate proves a raw DHCP discover/offer
exchange through owned DMA buffers and validates IPv4/UDP checksums where supplied.
The older configuration-only image fails that gate. The separate `--require-sana-device` gate proves resident driver query,
configuration, pending-read cancellation and two complete open/close cycles.
Neither gate alone proves SANA-II packet traffic or DHCP lease acquisition.
The separate `--require-network-stack` gate passes real AROSTCP startup and
DHCP lease acquisition through the resident SANA-II driver. A UDP socket round trip to the host NAT bridge also passes. The combined
desktop also passes DHCP-provided DNS resolution; TCP and sustained traffic
remain unqualified. See [network qualification](NETWORK.md). The separate `--require-network-irq` gate also requires an actual
network callback before processing completions. Both queues share the registered
PCI vector; packet draining stays in task context. The transport supports MTU up
to 1500 and requires
coherent PCI DMA; physical-platform cache/IOMMU contracts need separate work.

Standalone application: `tools/package-app.py` produces a private signed .app
with a probe-free seed. Bundled VZ mode passes first-launch copy, desktop/HID
startup, state/disk exclusivity and guest-acknowledged shutdown; host tests prove
later seed changes preserve personal disk bytes. Normal location is Application
Support/Macaros VM/Default. This is not Finder-click or VZ pixel verification,
and updated full guest reproducibility still needs comparison. Independent launcher
builds and same-input signed app packaging have passed their comparisons.
