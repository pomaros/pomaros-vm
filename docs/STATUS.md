# Qualification status

## Common descriptor service in the default runtime

The standard distribution builds and requires `fd.library` for both QEMU and
Virtualization.framework products. Without that shared descriptor service,
the native AROSTCP socket interface returns ENOSYS even though classic network
traffic and the desktop can work. A QEMU Rust poll probe exposed this omission;
image qualification must include the descriptor service, not infer its presence
from a desktop boot. `python3 -B tests/distribution-closure.py` exercises refusal
before packaging when the service is missing.

## Clean common-core reference candidate

CORE.lock selects local common main at
`61510f3cb1e28071a9f8d962f42b4135ad275412`. This clean public export separates
platform-specific DRM device enumeration and fixes documentation routes. Its
new build/runtime qualification is pending; preceding fc028f evidence retains
its original identity.

## Pre-export consolidation qualification

A separate CPUWidget production oracle passes offline against the retained actual
QEMU frame: exact static widget pixels, bounded dynamic text/gauge and 620
wallpaper blocks below the screen bar, plus eleven refusal controls. Its
`--pixels-only` mode makes no accounting/runtime claim.

The full QEMU product built from the same core passes compilation and packaging.
Its first supervised desktop attempt reaches accepted desktop, graphics IRQ and
two HID bindings with QMP/LLDB ready before execution, but fails its obsolete
kernel-widget marker. Its actual user-space CPUWidget displays unavailable
samples. The widget's reader is changed to the accounting contract's CNTVCT
origin rather than timer.device's relative EClock; target compilation is checked
separately and runtime confirmation is pending. This does not alter the earlier
VZ verdicts or qualify the new reader.

The local candidate pin is public-ancestry core `fc028f515e0d8b530b1da58ac036187c5ff133ed`,
checked out at the canonical `../aros-core` path. The canonical build is
`../Build/pomaros-vm/reference-core`. The VM build uses its own
GIC/timer backend, unrestricted guest x18 and an isolated SMP configuration.
The common core also places debug/processor/entropy in every normal desktop
build entrypoint, rather than only in a diagnostic cell. The transitive graph
regression rejects the preceding incomplete targets. The fresh normal desktop build passes in 723.02 seconds, including the three
residents without diagnostic-only make targets.

The preceding fresh common-only SMP build passed in 675.5 seconds, including AHI with no
physical Apple driver sources. The first HVF boot rejected a resident-count
mismatch before DOS startup. LLDB inspection found three required common
residents absent from the package: debug.library, processor.resource and
entropy.resource. The manifest includes them, the full desktop target builds
their closure, and the packager checks coverage against the pinned coldstart
table before writing output. Five closure tests and five package-admission tests
pass. The corrected package passes the resident stage. QEMU/HVF with its default
ITS controller cannot install the current GICv2m-only PCI IRQ route. Under VZ,
GPU IRQ completion, real timer waits, the IRQ chain, the Shell, the two-core
execution/memory probes and final startup contract complete. The full VZ verdict
is FAIL: expected CPU widget evidence is absent; the desktop-ready variable also
fails with the diagnostic image's missing ENV setup. The product Startup-Sequence creates and assigns ENV with its Sys directory.
The standalone CPUWidget compiles against the public SDK and waits for desktop
acceptance before opening. The SMP checker requires its real per-core sample
counts and shutdown; 19 negative controls pass. The first windowed run of this pin reaches CPUWidget ready with two cores,
two online CPUs and two samples. Its capture fails in the host ScreenCaptureKit
service, so the run remains FAIL and does not qualify the UI. Evidence is under
`artifacts/consolidation-common-fc028-widget-vz-2/`. The earlier VZ run remains refused. A bounded display-awake run passes in 36.96 seconds with GPU IRQ completion,
the two-core execution/memory probes, the 30-second desktop session, CPUWidget
shutdown and an owned-window capture. The captured image was inspected: Shell
and separate CPU 0 / CPU 1 rows are visible. Evidence is under
`artifacts/consolidation-common-fc028-widget-vz-awake/`. Keeping the host display
awake removes the capture-service failure in this run; the preceding failure
receipts remain unchanged. Native SysMon, input and networking are not qualified
by this gate. The archived preview
evidence below does not qualify this candidate. Nothing has been published.

The C pthread allocation-lifetime probe also passes 32 cycles: worker-allocated
objects remain readable and freeable after join; parent allocations can be freed
by a worker. The custom-startup VZ desktop gate passes in 36.96 seconds, with
GPU completion and the bounded session. Evidence:
`artifacts/consolidation-pthread-heap/`. This tests the SDK C link path, not
Rust's global allocator or every independent stdc opener. It does not justify
retiring Ferail's worker workaround until its Rust path is exercised.

The official Rust SDK is rebuilt against the consolidated native C SDK, with
compiler commit 504869653f510b279c542e65ccd1ea9710c119ba unchanged. A Rust System
allocator probe passes 32 transfers in each direction across joined worker exit
(8192 bytes). The Ferail ArosAlloc variant also passes 32 cycles each at 64 and
8192 bytes. Each bounded VZ desktop run completes in 36.96 seconds. The stdc,
posixc, pthread and stdcio import archives used by the native Rust SDK are
byte-identical to this VM SDK. Receipts and source hashes are under
`artifacts/consolidation-pthread-heap/`; the installed SDK records its own input
and product hashes. This contradicts treating worker-exit corruption as proven
for this cohort, but does not qualify the full Ferail application or native M1.
The earlier failures and original probe source snapshots remain retained.

The application disk builder can add identified executables and fixtures to its
separate WORK volume without rebuilding the common desktop. Exact readback,
metadata-independent image reproducibility, nested destinations, invalid paths
and existing-image preservation pass the actual mtools-backed gate in
`artifacts/consolidation-work-file-reproducibility.log`. Application runtime
acceptance remains separate from this image-construction result.

Ferail's functional example builds against the identified consolidated SDK.
The first application attempt cannot open bz2.library in the diagnostic image;
although the desktop gate passes, the complete 32-case checker rejects it.
A second image includes the common distribution runtime (2,138 files) but a
system requester prevents desktop acceptance before the application runs.
That run remains FAIL. A QEMU/LLDB preflight proves independent register access,
but its TCG replay stays in UEFI and does not diagnose this requester. Local
receipts: `artifacts/consolidation-ferail-functional/`. Full functional and GUI
qualification remain outstanding; no failure was relabeled as success.

## Public preview preparation

Version 0.1.1 (`v0.1.1-preview.1`) packages the qualified public core below. New signed runtime, Apple tickets and corresponding-source asset inventories are prepared separately; earlier 0.1.0 DMGs remain unchanged. Optional TrueType families outside the reviewed OFL/Apache/Bitstream set are excluded, with their selectors. Public publication follows the release coordinator’s final checks.

## Public-source qualification

The archived public-source qualification used `pomaros/aros-core` revision `89db046157d415e98c05ca18316ca7ae63b07023`. The final source was actually configured and rebuilt with frozen LLVM 20.1.0, UTC and retained release epoch `1792178266`; no archived binary was relabeled. Boot, Wanderer and the full canonical distribution graph pass, with all 44 required files present. DOS and AboutAROS binaries contain the final revision and public repository URL. The obsolete native updater backend is absent; its preferences show that updates are unavailable on this target.

Both fresh packages contain 58 resident ELF modules and exclude the eight physical Apple peripheral drivers. Their kernel ELF, Exec library and raw bootstrap are byte-identical; only the backend startup ramdisk differs. The actual kernel ELF SHA-256 is `22fed8070a01bf5b52da8eb9240c309587747283ea0f81fee5fd00a418189d19`. Evidence lives under local `artifacts/public-core-89db046/`.

The final windowed VZ stage6 run passes in 12.78 seconds: forced transport interruption and reopening, file exchange, clipboard reconnection, MultiView decoding, HID bindings, GPU completion and coordinated guest shutdown. Normal mtools reads both persisted fixtures, both FAT copies agree, the clean-shutdown bit is set and the immutable seed is unchanged. Evidence: `artifacts/stage6-public-89db/`.

A separate production run reached the desktop but failed its requested ScreenCaptureKit capture with an audio/video capture-service error; cleanup still received guest quiesce. That run retains its FAIL verdict in `artifacts/desktop-production-public-89db/`; it does not replace the archived README image. The public UP monitor displays clock and CPU rows without native controls. Clock accuracy, sensor values and broad desktop/input/network stability are not qualified.

The tested app records actual VM product revision `5896047`, guest revision `89db046`, and its immutable seed hash. Later checker/documentation commits do not rewrite that application manifest. Existing notarized distributions below remain unchanged; these fresh public-source development apps have ad-hoc signatures and were not substituted into those DMGs.

## Archived image and earlier evidence

The earlier development image builds and boots its full AROS desktop using Apple's Virtualization.framework. The README image is an unmodified capture of the actual windowed application, including its title bar and the PomAROS monitor with both project URLs. It uses an isolated disk; personal profiles were not opened or changed.

## Archived development qualification

The archived qualification below used shared core pin `9f346ab00d644bd40e0f53e2624c3f625d28f182`. The guest SDK was built fresh with frozen LLVM 20.1.0, using release epoch `1792169063` and UTC. The full distribution graph and all 44 required-file admissions pass. Both backend distributions and their signed development applications package successfully.

Actual guest objects were compiled at `48e445615380448d74ec02c9958a71201ce4904c`. Subsequent shared-source changes add a hosted-only ELF protection correction, extract the unchanged monitor renderer into a shared header, and add a Darwin-only SystemMonitor utility. The standalone loader tokens and the entire guest coldstart translation unit retain identical preprocessed code. Qualified artifacts and runs keep their original source metadata at `3c382bc`; this source-pin update does not relabel those runs or rebuild unchanged guest objects. No earlier compiler's guest objects were imported.

The final VZ stage6 run passes in 18.6 seconds: forced vsock interruption, guest session reopening, file PUT/GET, clipboard reconnection, MultiView JPEG decoding with varied pixels, desktop stability, two USB HID bindings and GPU IRQ completion. One window-close request receives the guest quiesce acknowledgement and exits normally. Normal mtools reads both persisted file fixtures; the two FAT copies agree and the FAT16 clean-shutdown bit is set. The immutable seed remains unchanged. Local evidence: `artifacts/stage6-final-3c/`.

A separate production desktop run injects no guest probe, captures its real window through the public current-process ScreenCaptureKit API, and closes through the same quiesce path. It passes in 8.99 seconds. This produces `docs/images/pomaros-vm.png`; local evidence: `artifacts/desktop-production-final-3c/`. Capturing one's own window requires macOS 14.4 or later and does not request global screen access. Normal VM operation retains its macOS 13 requirement.

Swift host/service/storage checks, firmware FDT sanitizer checks, generated protocol consistency and packaging/source/backend admission checks pass. The xHCI capability and port register accesses were checked in actual compiled objects before the successful HVF desktop runs.

The final source pin also maps macOS EPERM to AROS write protection in the Unix hosted filesystem handler, with a production-code read-only fixture and a negative control. This hosted-only correction changes no standalone guest code. Source equivalence was rechecked; the sealed guest distributions below keep their original 3c/48e provenance and were not rebuilt or re-signed for it.

## One guest kernel, two backends

Actual final VZ and QEMU bundles were parsed and their payload hashes independently verified. `kernel.resource`, `exec.library` and the raw stage2 bootstrap are byte-identical. Only the `SYS` ramdisk component differs, reflecting backend startup and host-service configuration. The kernel ELF SHA-256 is `646c209ca0cc241d7286522df40f7cafbbcf334a20c8d6574e8d45d7463438a7`. Local evidence: `artifacts/pomaros-migration/final-3c-shared-kernel-proof.json`.

The macOS launcher uses Apple's ABI. The VM guest leaves x18 unrestricted; only the hosted target reserves it. Native Apple and future Raspberry Pi share the ordinary guest register policy and common source, but their hardware is not qualified here.

## Limits

These bounded checks do not establish long-term desktop stability, real keyboard/pointer event delivery, full networking, audio or production SMP. QEMU has no VZ clipboard/file provider or coordinated guest flush/quit transport; its separate status documents that limitation. The earlier failed HVF/MMIO, screenshot and FAT runs remain preserved in local artifacts; their failure verdicts were not rewritten.

The product repositories and curated shared [AROS core](https://github.com/pomaros/aros-core) are public. The historical core commits used by the archived signed artifacts remain in the private lineage. Native hardware development is maintained separately and is excluded from this product. Build procedures live in [BUILD.md](BUILD.md) and [DISTRIBUTION.md](DISTRIBUTION.md); architecture lives in [PORTABILITY.md](PORTABILITY.md).

## Signed internal distribution

The exact Developer ID signed application passes a separate 17.85-second windowed production run, owned-window capture and coordinated guest quiesce. The immutable seed is unchanged.

`PomAROS-VM-0.1.0-arm64.dmg` is signed by Developer ID Application: John Knipper (C43N3NG7Z5), with hardened runtime and a secure timestamp. Apple accepted the application ZIP (`806ea754-c7d2-4e3c-9c12-d9a77b118a32`) and final DMG (`98fe5d35-3b3a-4b86-a64f-eee5839b67b1`). The app was stapled before building the DMG; the final image is also stapled. Both signatures, Apple tickets and Gatekeeper checks pass.

Final post-staple DMG SHA-256: `1632467a6f62d732a55312b5ac7ee7c0068e5520924678c0d2ac70d0547601d5`. Local runtime and submission evidence lives under `artifacts/release-20261016-*` and `artifacts/notary-20261016-*`. See [SIGNED-DISTRIBUTION.md](SIGNED-DISTRIBUTION.md) for the reproducible signing and verification procedure.

These are internal development artifacts for John. No public binary download or GitHub Release was uploaded. The new public curated source does not contain the historical private source commits identified by these archived binaries. Recipients of those binaries still need their exact corresponding APL/GPL/LGPL and other component sources before redistribution. A future public-source build must be qualified separately; no archived binary was relabeled.

## Public-source migration

Builds acquire the curated official-history `pomaros/aros-core` fork through `CORE.lock` into the separate `../aros-core` checkout. The VM boot manifests exclude physical Apple PMGR, DART, RTKit, SPMI, SMC, PCIe, NVMe and SPI-HID drivers. `appleboot.device` remains because it supplies the VM RAM-backed system volume. The source acquisition and native-module admission checks do not establish a successful new guest build or boot. The archived runtime evidence above belongs to the unchanged earlier artifacts.

## Configurable application capacity

The launcher accepts bounded guest memory in MiB. WORK geometry is recorded
by packaging and validated by disk creation, with sizes from 256 to 1024 MiB.
The merged implementation retains identified WORK fixtures, owned-window
capture and the independent host-services admission flags. Launcher refusal,
package-backend and reproducible disk tests pass, including the 512 MiB WORK
case and corrupt/mismatching geometry refusals. These are host checks; no
larger-memory guest or application execution is claimed from them.
