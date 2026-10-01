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
| 3. VZ desktop and input | Wanderer appears under Virtualization.framework and accepts keyboard/pointer input through guest drivers | Wanderer renders verified pixels through the resident virtio graphics HIDD under HVF (EFI regression also passes). The same desktop guest passes startup/foreground checks under VZ. VZ host-view pixel capture remains unverified; an AROS ReadPixelArray capture of a MultiView JPEG window passes (see current qualification below). Resident GPU commands now wait for PCI interrupts with a one-second timer bound; a 20-second VZ desktop session passes the IRQ gate alongside USB/HID, DHCP/DNS and clipboard. The interrupt-driven GPU also passes the HVF pixel/startup gate with GICv3 and GICv2m after constraining MSI-X MMIO accesses to plain scalar instructions. VZ input exposes Apple PCI xHCI (not virtio-input); capability mapping and actual driver OpenDevice/query pass after removing the legacy INTx-line admission restriction. Poseidon enumerates the root hub and reads two child descriptors under VZ after PCI-owned payload integration and observed-bit PORTSC acknowledgment. The desktop SYS startup loads Poseidon/HID and two configured child HID bindings pass with Wanderer startup. A bounded 30-second desktop session with shell, 15 ticks and completed startup passes under VZ. A targeted VZ-view event sequence reaches an Intuition window through USB/HID (key down/up, button down/up, two coordinate-checked moves and native-width tablet button attributes). Physical input and cancellation/teardown fault coverage remain unqualified; bounded integrated usage now passes 60 file/WORK cycles alongside the desktop. |
| 4. Persistent storage | Guest writes, flushes, remounts and verifies data after a full VM restart; controlled interruption recovery | Boot images and acknowledged virtio reset/start/stop probes pass in VZ/HVF. Shared virtio block transport/queue reads a boot sector in VZ/HVF. Shared MSI-X queue routing passes in VZ; full device startup and IRQ-backed CMD_READ pass in VZ. Isolated 4 KiB write/FLUSH/readback and fresh-VM persistence pass in VZ; Mounted FAT mkdir/write/rename/flush and fresh-VM file verification pass; One abrupt open-file write workload passes SIGKILL, read-only FAT consistency and fresh-VM durable-file verification; interruption coverage remains limited; a 107.61-second desktop workload passes 60 write/rename/readback and host file-exchange cycles with clean stop and independent final WORK bytes. Optional desktop WORK: auto-mount now uses standard Mount and virtio.device. The desktop path now passes 64 KiB write, FAT drain, negotiated virtio FLUSH and fresh-VM read with independent host bytes, alongside USB/DHCP/DNS/clipboard. Device/volume naming and startup publication races were corrected. Host-requested shutdown now waits for VMSync to inhibit and flush VMWORK0 before stopping; a bounded SIGTERM/guest-ack/host-byte check passes. An isolated VZ run triggers the actual NSWindow.performClose delegate path and verifies one guest quiesce, one host acknowledgement and clean exit; physical mouse clicking remains unmeasured. Unsaved application buffers and other volumes are outside this contract. Embedded SYS is RAM-backed. |
| 5. Complete reproducible build | Empty build directory produces guest, SDK, commands and launcher; independent rebuild comparison | Two independent builds of the current core, including GPU IRQ and MSI reservation fixes, pass all 22 targets: 18,285 product/SDK entries match and all 28 required AArch64 products are present. The launcher and EFI have separate independent-build comparisons; outer boot/WORK FAT images and signed app files have deterministic-packaging evidence. A signed application assembled directly from the current complete guest build passes one 8.66-second isolated-profile VZ check: desktop startup, GPU IRQ completion, two HID bindings, state/disk exclusivity, unchanged seed and guest-acknowledged shutdown. This is a headless startup check, not VZ pixel qualification or a repeat of sustained usage. Exact revisions, hashes and artifacts are recorded in the handoff. |
| 6. Prolonged use and integration | Sustained desktop/storage/network exercise; clipboard text and images in both directions; usable file exchange | Raw virtio-net TX/RX with a transaction-checked DHCP discover/offer passes in VZ NAT. Resident SANA-II OpenDevice/query/configuration/read cancellation/close/reopen passes in VZ. AROSTCP obtains a DHCP lease through resident SANA-II in VZ NAT. An AROSTCP UDP round trip with an independently checked 32-byte host challenge passes via the Mac NAT bridge address. Earlier attempts through the Mac Wi-Fi address failed; their cause remains unproven. A bounded 32-byte vsock stream round trip passes between AROS and the VZ launcher. The resident vmbridge.device passes two 64 KiB Exec IO cycles with pending-read abort, write drain, IRQ completions and close/reopen. Host file-service socket-pair tests and the VZ gate pass: upload/readback of 64 KiB, no-replace/path refusal, and standalone VMExchange HELLO with reopen. A focused QEMU gate also verifies nested command startup. CLI PUT/GET, existing-destination refusal and guest-file persistence across two fresh VZ VMs pass with independent host byte checks. A bounded live desktop workload now passes 60 WORK write/rename/readback and actual VMExchange PUT/GET cycles, with independent host verification and graceful shutdown. The host clipboard endpoint passes named-pasteboard/socket tests. Two fresh VZ boots pass real clipboard.device text/image synchronization, host-wins conflicts, unchanged revisions and normal session reconnects; standalone VMSync ONCE imports correctly. A bounded 20-second VZ Wanderer session verifies the actual auto-started daemon through a separate clipboard-device client: text/images, foreground session contention/recovery, independently loaded VMExchange HELLO and no echo. Existing AROS Editor text Paste/Edit/Save/Select All/Copy passes through VZ-view input, with independent WORK-file and private Mac pasteboard bytes plus clean shutdown. A MultiView JPEG window with nonflat guest pixels and a forced active vsock cut followed by guest file PUT/GET and clipboard-daemon reconnection now pass in a bounded VZ run. Physical shortcuts, host-view pixels and prolonged reliability remain unqualified. The combined 20-second desktop session passes AROSTCP auto-start, DHCP and DNS resolution through the DHCP-provided resolver alongside clipboard and USB/HID. Missing T: caused the earlier DHCP-client requester; normal MakeDir/Assign startup fixes it without relaxing desktop acceptance. A bounded desktop TCP stream passes 64 KiB upload plus transformed download, independent host byte verification, half-close and EOF. A 32-connection local TCP workload completed 2 MiB in each direction with independent byte checks; captured-run reassessment passes after correcting a one-tick heartbeat tolerance (original runner timeout preserved). HTTP and sustained public Internet usage remain unqualified. An independently captured early-console TX timeout path is corrected and eight VZ boots pass; a 107.61-second integrated run passes 60 IO/file-exchange cycles, 270 bridge closes and independent final bytes after graceful stop. It exposed and corrected per-session MSI source exhaustion; reservations now persist per device, with 512 reopens covered under sanitizers. This remains bounded usage, not an exhaustive reliability claim. |

Next implementation boundary: VZ desktop visual/input acceptance and cross-backend GPU IRQ qualification, with
broader interruption coverage still required; bounded sustained WORK/file exchange is qualified. The ACPI bridge accepts GICv3 only, while the PCI interrupt provider supports GICv2m frames only. Switching the QEMU desktop to GICv2 does not resolve this gap: its ACPI topology would be rejected before PCI initialization. QEMU 11 also offers `msi=gicv2m` with GICv3, so ITS implementation is not a prerequisite for that alternate configuration. The HVF GICv3/GICv2m configuration passes current GPU IRQ completion and desktop pixels after the MSI-X access correction. A volatile C table read had compiled to a post-indexed instruction; explicit scalar MMIO accesses remove the observed ISV assertion. ITS remains unqualified. The later one-file correction has targeted build/runtime evidence, separate from the preceding complete independent rebuild. Controller/bus startup failure
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
desktop also passes DHCP-provided DNS resolution; A local NAT TCP stream passes 64 KiB in each direction with independent byte
checks; HTTP and sustained traffic remain unqualified. See [network qualification](NETWORK.md). The separate `--require-network-irq` gate also requires an actual
network callback before processing completions. Both queues share the registered
PCI vector; packet draining stays in task context. The transport supports MTU up
to 1500 and requires
coherent PCI DMA; physical-platform cache/IOMMU contracts need separate work.

Standalone application: `tools/package-app.py` produces a private signed .app
with a probe-free seed. Bundled VZ mode passes first-launch copy, desktop/HID
startup, state/disk exclusivity and guest-acknowledged shutdown; host tests prove
later seed changes preserve personal disk bytes. Normal location is Application
Support/Macaros VM/Default. This is not Finder-click or VZ pixel verification,
and does not qualify GUI editing commands. The current full-build comparison
includes the GPU IRQ/MSI changes. Independent launcher builds and same-input
signed app packaging have passed their comparisons.

Corrected app artifact and profile update: the private app with qualified GPU/MSI
components passes bundled startup and clean quit on an updated image retaining
existing WORK files. The no-replace image updater preserves WORK byte for byte
and refuses active VZ disks. That earlier component-overlay application has been superseded by a directly
packaged complete-build application with a separate startup/clean-stop proof.
The updated-profile preservation proof remains a distinct earlier test; see
the handoff for exact identities.

### Host transport interruption boundary

The clipboard service now has focused socket-pair recovery coverage: truncated
headers/bodies, partial and complete unpublished uploads, new-connection state
isolation, lost publication reply, stale retry refusal and subsequent successful
publication on the same server. Private named pasteboards protect user content.
This qualifies the host protocol state machine, not the guest daemon's recovery
after a forced virtio-vsock failure. That end-to-end test remains open.

### Stage-six integrated recovery and image check (2026-10-01)

An isolated copy of the DeviceTree distribution disk was patched with
test-only boot probes. The production host accepts a `--transport-cut-test`
request file only when that explicit option is present. It calls `shutdown`
on the active virtio-vsock connection, leaving VM and service alive. The
guest's in-flight request then fails, a new session succeeds, and `VMExchange`
PUT/GET returns exact independently read bytes. The automatically running
clipboard client reconnects and successfully requests a snapshot over a new
connection. A test-only `--window-close-test` request calls
`NSWindow.performClose`, reaching the normal delegate. Exactly one guest
volume quiesce and host acknowledgement precede exit zero.

In the same 17.06-second VZ run, `MultiView` opens an included 800×600 JPEG.
The public screen has a titled 822×638 MultiView window;
`ReadPixelArray` returns 16,384 RGB pixels, 16,288 different from the first
pixel in a region inside the window. This proves guest GUI image decoding
and drawing. A human still needs to judge what appears in the macOS VZ view;
macOS screen-capture preflight is false for this agent. No physical input or
Raspberry Pi result is inferred. Evidence:
`artifacts/vz-stage6-image-cut-close-2/verdict.json`, console and the
`stage6-image.txt` read back from WORK after clean stop. The first cut-only
run also passed; the first image attempt did not start because a test-only
probe had a compile-time format warning, fixed before the combined run.
