# Qualification status

## Native path candidate

Candidate promotion is withheld. Two further runs complete graceful volume
quiescence in about 11s, but both contain a suspended Wanderer NULL-read crash
while handling shutdown. Their original PASS verdicts are withdrawn by
`acceptance-review.json` beside the original logs in
`artifacts/consolidation-native-path-reference/stage6-current/` and
`vz-production-after-stage6/`. Both acceptance tools reject guest task crashes,
including crashes after window close; nine desktop-contract regressions pass.
The initial 67.48s failure first timed out waiting for desktop readiness;
its close request was cleanup, not a successful desktop followed by shutdown.
The same immutable launcher/seed can reach the desktop and quiesce, so neither
an unconditional VMSync transport failure nor a disk-flush defect is established.
The recorded Wanderer crash requires debugger reproduction before promotion.

Common candidate `7bc6a38c6c2` completes the fresh 87-target SMP SDK build.
The SDK-linked client passes native exclusive and shared CurrentDir, the
CRT worker/errno cases, RAM/FAT resize and two-CPU SMP in 40.81s, following an
independent LLDB register/memory/step/resume-interrupt preflight. Evidence:
`artifacts/consolidation-native-path-reference/qemu/`.

The version2 DOS resolver policy preserves native exclusive Process operations
without lending their locks; ordinary Tasks and explicit operation/selected
context scopes stay mediated. The actual ownership/snapshot/policy fixtures
pass seven named compiling mutation controls, and the actual vfork scope gate
passes. Full native hardware acceptance is separate.

The real VZ network gate passes DHCP, DHCP-provided DNS and three checked
64KiB bidirectional TCP transactions with both guest and host-peer acceptance
in 23.71s, alongside desktop/HID/GPU IRQ. The live host NAT endpoint is verified.
Evidence: `artifacts/consolidation-native-path-reference/network-complete/`.
The preceding diagnostic package omitted the SDK's DHCP service executable
and service definitions; its DHCP failure remains under `network/`. The complete
production runtime already contained these files. Packaging includes the
existing service payload and refuses missing inputs; no stack/driver rewrite
was made. The raw VZ bundle inspected under QEMU stopped at its bootstrap
contract before the desktop workload, so it is not a DHCP reproduction.

The first full signed VZ production attempt times out before desktop readiness.
Its cleanup dispatches window close but receives no quiescence acknowledgement;
forced cleanup ends the run after 67.48s. This is FAIL, not reference promotion.
Evidence: `artifacts/consolidation-native-path-reference/vz-production/`.
The source of the missing shutdown reply is under investigation; neither a
network-service cause nor an Inhibit cause is established by the timeout.
The host sample does not show an active blocked host-service worker.

The existing standalone desktop-work probe passes on QEMU TCG using the actual
product machine/device configuration and this candidate SDK: 64KiB write/Flush,
volume Inhibit, virtio CMD_UPDATE and remount. The existing network stack starts
in the same guest. Independent LLDB registers/memory/single-step before boot
and interruption after the workload pass; QMP terminates the owned guest cleanly.
Evidence: `artifacts/consolidation-native-path-reference/qemu-volume-debug/`.
This narrows the investigation to the VMSync task/host-service context; it does
not prove VZ shutdown works or that Inhibit cannot fail in that context.

A direct VZ GDB attempt confirms that the private stub configuration exists
but the VM cannot start under the ordinary signing entitlements. The attempted
host code was discarded and the normal launcher rebuilt; no host security or
entitlement settings changed. QEMU remains the independent guest debugger.
Matching-SDK stage6, shutdown correction and coordinated product/native alignment
remain open. `CORE.lock` retains the preceding reference.

## Native CurrentDir correction and candidate desktop

The complete desktop package from common core
`b6013ea4f2b04f3ec669606ec87f17ba9d9a3694` builds with 3,875 runtime files.
Its signed VZ production test fails: an AROSTCP NetDB error requester prevents
CPUWidget desktop acceptance. The owned-window inspection shows
`Fatal error in NetDB file Work:System/System/Network/arostcp/db/netdb at line 1, col 25`:
`readnetdb: object not found`. The packaged netdb includes `interfaces`, and
that file is present. Inspection exits normally through window close.
Evidence: `artifacts/consolidation-common-b601-vz-production/` and
`artifacts/consolidation-common-b601-vz-inspection/`.

The first native-CWD fixture used CreateDir's exclusive lock; LLDB inspection
shows that its copy fails with DOS error202, so that failure does not establish
the NetDB cause. The corrected fixture releases that lock and takes a shared
Lock, matching NetDB. Common candidate `35fb4bf30ec` completes the fresh
87-target SDK build and the full two-CPU QEMU CRT/RAM/FAT/SMP gate in40.21s,
with independent LLDB preflight. Native CurrentDir/relative DOS Open passes.
Evidence: `artifacts/consolidation-native-cwd-shared-fixed/`. The exact shared-lock client fails on the previous b601 boot bundle, with
only its SYS client replaced; all kernel/resident component bytes are unchanged
(`artifacts/consolidation-native-cwd-shared-old-bundle/control-result.json`).
Overriding CRT files on SYS alone is not an old-code control because resident
CRT modules take precedence; that discarded attempt is explicitly marked invalid.

The signed production VZ desktop passes normal startup, window capture and
graceful quiescence in12.53s, with the normal network stack enabled and no NetDB
requester. Matching-SDK stage6 passes cut/held-session refusal, reopen, file
PUT/GET, clipboard reconnect, JPEG and clean FAT shutdown in11.20s. Evidence:
`artifacts/consolidation-native-cwd-fixed/vz-production/` and `vz-stage6/`.
These do not establish external network traffic or native installed acceptance.
The exclusive-lock limitation is open: native CurrentDir with CreateDir's
exclusive lock cannot be copied into the owned resolver context (DOS202).
`CORE.lock` retains the preceding reference pending coordinated product
qualification and review of that native API regression.

## Candidate CRT and writable FAT qualification

Common source `b6013ea4f2b04f3ec669606ec87f17ba9d9a3694` completes the
87-target SMP runtime build. Its SDK-linked runtime-context client passes the
combined two-CPU QEMU TCG CRT/SMP gate in 28.85 seconds, following independent
LLDB register/memory/step/resume-interrupt preflight. Both RAM and writable FAT
native/CRT resize, zero extension, position preservation, metadata and errno
checks complete. Evidence: `artifacts/consolidation-common-crt-fat/`.

The first fixture wrongly targeted read-only boot SYS:. Debugger inspection
locates its refusal at FAT stage 60, IoErr 214. The corrected client requires
`--resize-path`; QEMU attaches an isolated copy of a geometry-checked WORK
image through `--work-disk`. The original image hash remains unchanged. Seven
invalid runner profiles and eight marker-contract negative controls pass.

This is a candidate regression, not product-pin promotion, VZ production,
application or physical hardware acceptance. No QEMU diagnostic trace was
requested. Three non-GPU enumeration refusals retain a stale IRQ-lock stage
label; subsequent graphics registration and the SMP workload complete. The
separate headless command-startup check and coherent product qualification are
pending; `CORE.lock` remains the reference identified below.


## Current common-core reference

`CORE.lock` selects common core `63de2673bec7b6c1c4226a7909ec9614d5cc4a91`.
The complete 83-target runtime and image assembly pass. A locally signed VZ
production app passes owned-window capture and graceful volume shutdown in
12.39 seconds. Matching-SDK stage6 passes forced transport cut, held-session
failure, reopening, file PUT/GET, clipboard reconnection, JPEG rendering and
clean FAT readback in11.28 seconds. QEMU TCG separately passes the bounded
two-CPU Exec/x18/FPSIMD/timer/wake/teardown and Virtio IRQ scenario in29.02s,
with an independent LLDB preflight. Evidence:
`artifacts/consolidation-reference-vz-production-awake/`,
`artifacts/consolidation-reference-vz-stage6/` and
`artifacts/consolidation-reference-qemu/`.

The first VZ attempt fails its window capture while the main display is asleep
(online1, active0, asleep1). Waking the display changes it to active1/asleep0;
the same unchanged app then passes. Capture test runners hold a temporary
user/display assertion tied to their owned VM process and release it on exit.
They do not change power preferences or skip the capture gate. Evidence:
`artifacts/consolidation-vz-capture-diagnostic/`.

This reference does not establish semantic physical input, application GPU
acceleration, broad driver/SMP workloads or native Apple hardware behaviour.
The app and probe manifests keep their exact producer and guest identities;
promoting this source pin does not relabel older binaries.

## Local artifact retention

11 superseded generated application bundles are removed from local
artifact runs. The core-63de2673bec reference application is retained at
`artifacts/consolidation-reference-vz-distribution/PomAROS VM.app`. Test verdicts and provenance metadata remain available;
separate disk profiles are preserved. This cleanup does not change source
pins, qualify a candidate or relabel an existing binary.

## CRT and file-resize candidate

Common core `a7c9dc7401cd1c4898385d9a4dbbf9cae6913ddb` passes the isolated SMP
runtime build (initial 83 targets in 744.66 seconds, expanded 87-target closure
in 169.83 seconds), strict client link and image packaging. Explicit stdc,
stdcio and posixc modules are required by packaging on both backends; the host
closure regression passes. The exact client-result contract rejects seven
controls, including the former narrower context-only success marker.

The expanded two-CPU QEMU gate fails in 13.09 seconds. Independent LLDB
register/memory/step/resume/interrupt preflight passes. Symbolic breakpoints
identify stage 35 on RAM: native DOS growth and the preceding CRT checks pass,
as do all positive wide seek/resize/stat calls. ftruncate64(-1) returns -1 but
the caller's errno remains zero. Instruction breakpoints verify its negative
argument, return and unchanged errno pointer/value. The client and shared stdc
have separate errno-hook globals; shared error ownership needs correction.
The diagnosis is recorded in `artifacts/consolidation-crt-resize/summary.json`.
Diagnostic VMs are stopped. This candidate is not promoted; the production
source lock and its previously qualified images retain their exact identities.
FAT client cases follow the failed RAM case and have not executed.

Source correction `550f8a5a24e` introduces shared native task errno ownership,
StdC worker admission, owner/binding close guards and program-entry reset.
Scoped ASan/UBSan, TSan, pthread admission regressions and six AArch64 TUs pass.
The public StdCBase layout changes; runtime plus Ferail/Zed/Chromium consumers
must be rebuilt coherently. The first fresh 87-target build stops in 150.16
seconds because the early stdc import archive lacks generated task.resource
headers. Build fix `443b0e68138` adds the missing MetaMake prerequisite; its
replacement ABI build passes all 87 targets in 787.38 seconds, then strict
client link and image packaging pass. Independent LLDB transport preflight
passes. The guest gate times out after 90.14 seconds following input.device
dead-end device-open alert 0xB5040000, with no CRT client result recorded.
LLDB identifies keyboard.device's nested keyboard.hidd open refusal and
establishes that errno initialization rejects task.resource because its
resource header leaves lib_NegSize zero. Core b061818e34f admits the published
API version instead; its complete 87-target build passes in 843.87 seconds.
The correctly configured gicv2m guest reaches RAM stage 36. A breakpoint then
captures read-only ftruncate returning 0 with errno 0: the guard uses Unix-style
access-mode assumptions, while AROS uses read/write bits 1/2/3. Core
3982c72e268 corrects the write-bit check; the fixture imports the public AROS
constants and passes 19 ASan/UBSan and TSan records plus two complete AArch64
translation units. Linked correction and FAT guest cases remain unqualified.
The superseded intermediate a7c9dc SDK is retired; evidence is retained. Failed generated SDK outputs are retired,
with logs retained and downloaded archives moved to the shared cache.
The correction has not yet passed the linked guest gate. The client now
requires initial worker errno zero, shared-library EBADF propagation and creator
isolation across join. Its exact-result host contract passes eight rejected
controls. The freestanding
static bootstrap object compiles without shared imports; static threaded C is
outside this qualification. No current product pin or binary is promoted.

## Shared DeviceTree host regression

`tests/device-tree.py` delegates to the selected common core's
`developer/debug/test/openfirmware_host/run.py`; the product carries no second
command fixture or expectation table. `AROS_CORE_SOURCE` selects a core Git
checkout containing that shared runner, defaulting to the adjacent `aros-core`.
The command and fixture at core `bee943f575c` pass fourteen sanitizer scenarios
and an exact ROM-tag dependency mutation in
`artifacts/consolidation-device-tree-shared-final/`. The old VM fixture's
expectation of a ROM tag for a disk-loaded resource was stale. Host resource
construction also passes twenty-one nodes and seventy-two allocation failures
with the explicit shared SMP SDK. Neither result promotes CORE.lock or qualifies
a guest/native runtime.

## Earlier Exec SMP checkpoint

Common core `434885690560824e84ffa17d2c97117a41895ca3` passes the bounded
windowed TCG two-core Exec gate in 27.58 seconds. QMP verifies a paused guest with two CPUs before execution; guest LLDB lists
both threads and reads initial registers before resuming. The gate requires overlapping computation with x18/FPSIMD
preservation, timer delivery under computation, wake/teardown, both CPU widget
records, widget capture and secondary CPU shutdown. Virtio graphics IRQ
completion also passes. Evidence is
`artifacts/consolidation-shared-smp-handoff/verdict.json`; exact tested runner
bytes are recorded beside it. Physical hardware, general drivers and network
qualification are outside this result. Product CORE.lock is not promoted.

The first startup failure was a mismatched storage test profile: the debugger
stops at Assign returning 20 for an absent appended WORK volume. The standalone
ESP runner now refuses that incompatible profile before boot. A subsequent
USB-desktop run exposed absent virtual xHCI hardware; the runner provisions the
USB devices declared in the package. Neither preparation error required an OS
change. Four profile/debugger rejection cases pass before output creation.

## Host GNU make selection

The core-target and distribution builders select GNU make 4 or later and pin
nested literal `make` calls to it. `AROS_MAKE` selects an explicit executable;
missing/old overrides are rejected. The actual nested response-file test passes
with a deliberately misleading make 3.81 earlier in PATH and verifies shim
cleanup on success and injected failure (`make test-host-make`).
Core-target requests use one MetaMake graph after a shared bootstrap. A fresh
SDK attempt exposed two simultaneous `.DEFAULT` recipes configuring MetaMake
in the same directory; its retained log contains interleaved configure probes
and corrupted feature detection. The real GNU make boundary regression checks
one bootstrap, grouped graph roots and cleanup after a failing graph. The
corrected fresh SMP POSIXC/pthread module rebuild passes in 315.284 seconds
and the complete 83-target runtime passes in 600.567 seconds, from clean core
`5663d3874bec85bfcca71a3a549ab668514dbf48`. The canonical runtime-context
client passes the two-CPU QEMU SMP/startup gate in 29.25 seconds: selected cwd,
retained descriptor and relative open survive a code-pin instance switch.
Product CORE.lock keeps its qualified source cohort; this focused candidate run
does not qualify all context consumers, hot reload, applications or native Apple.

The client initially failed at write with EBADF. Live LLDB inspection located
stage 6 and an Exec pointer returned by open. Native exec_regcall_stubs.o exports
strong library lifecycle symbols named open/close; the builder selected those
before the POSIXC weak wrappers. Resolving the CRT archives first fixes the
client without changing the OS. The guest regression requires both its unique
PASS marker and the complete normal startup/SMP verdict. The runner's
`--require-runtime-context` enforces this directly; the final run passes in
31.54 seconds with `runtime_context_verified=true`. Its exact-marker parser
rejects six missing/duplicate/partial/contradictory controls. Debugger transport,
section-specific symbol loading and exploratory-run boundaries are documented
in BUILD.md. Hardware watchpoints hung QEMU 11.0.1 MTTCG in host watchpoint/TB
invalidation; software breakpoints supported the diagnosis.
The older direct generated-makefile/header-copy helper is removed; configured
reference builds and isolated candidates use `tools/build-core.py`.
The fresh SMP candidate rebuild passes payloads in 312.63 seconds and the
complete distribution in 369.8 seconds; the initial make 3.81 failure is kept
separately. Runtime evidence is scoped in the SMP section above.



## Production CPU widget readiness

The VZ distribution checker requires one complete CPUWidget report with one
core, one online CPU and one sample, plus exactly one stable desktop,
accepted desktop and two-HID-binding marker. Missing, duplicate, partial or
inconsistent reports are refused. The native Alive-window marker is not part
of this guest profile.

The first aligned-core VZ run reached desktop acceptance in 3.508 seconds and
quiesced the volume on window close, but its checker timed out waiting for that
obsolete marker. Its original failure remains in
`artifacts/consolidation-aligned-vz-desktop/`. The corrected strict checker
passes in 11.25 seconds with a captured, nonuniform guest viewport, exact
readiness counts, GPU completion, HID bindings, guest quiescence and unchanged
seed. Evidence: `artifacts/consolidation-aligned-vz-desktop-strict-fixed/`.
Eight desktop-contract tests refuse inconsistent readiness, invalid geometry
and blank guest pixels even when the macOS title bar contains content. The
window capture check does not claim QEMU's pixel-exact glyph/wallpaper oracle.
An intermediate missing-import failure and the original timeout remain intact;
three superseded runtime disk copies were retired after verified quiescence,
with hashes in `artifacts/consolidation-redundant-vz-disks-retirement.json`.

The matching-SDK stage6 run passes in 11.29 seconds: forced transport cut,
held-session failure, reopen, file put/get, clipboard reconnection, MultiView
JPEG pixels, capture and coordinated shutdown. Both persisted fixtures are
readable with normal mtools; FAT copies agree, the clean-shutdown bit is set,
and the seed is unchanged. Evidence: `artifacts/consolidation-aligned-vz-stage6/`.
VM and QEMU packages consume the same configured guest objects at
`../Build/pomaros-qemu/rust-aligned-core`; Hosted binaries remain a distinct ABI.
The immutable app records its actual producer revision; later checker changes
do not relabel that artifact.

## Common descriptor service in the default runtime

The standard distribution builds and requires `fd.library` for both QEMU and
Virtualization.framework products. Without that shared descriptor service,
the native AROSTCP socket interface returns ENOSYS even though classic network
traffic and the desktop can work. A QEMU Rust poll probe exposed this omission;
image qualification must include the descriptor service, not infer its presence
from a desktop boot. `python3 -B tests/distribution-closure.py` exercises refusal
before packaging when the service is missing.

## Aligned common-core reference

The preceding reference selected common main
`afc0dbeefa12d65030ec0db8cd152a5518531913`. QEMU consumes the shared packaging
code and passes the corrected default-runtime desktop in 20.71 seconds, with
QMP/LLDB ready before execution, live CPU samples and strict frame controls.
The same image passes all six Rust socket-poll cases in 18.2 seconds under TCG
with its guest debugger attached before execution. These are local QEMU
qualifications. The VZ desktop and stage6 qualifications above cover the same
pin on Virtualization.framework. Application and physical native GPU acceptance
are separate. Older evidence below retains its original source identity.

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

## Consolidated cohort preparation

The SMP payload build from common core `54acf75075cd7c6e6f576df3a18df1e91edfe621`
completed in 693.20 seconds with unchanged source identities. Evidence is under
`artifacts/consolidation-current-cohort/`; no guest acceptance is claimed.
The completed common-core cohort uses `63de2673bec7b6c1c4226a7909ec9614d5cc4a91`,
including empty lifecycle-set definitions and remote Signal/Virtio waiter fixes.
Its explicit candidate lock was promoted after the current-reference checks above.

The configuration tool checks its selected build Python before creating output.
It preserves the venv executable path rather than resolving its final symlink to
the system interpreter. A real symlinked-venv regression verifies that dependency
imports use the selected environment; pin/ABI refusal tests also pass. The failed
configuration did not create a build directory. Other product pins and qualified
images retain their recorded source identities.

The runtime build bootstraps configured files, the cross toolchain and host
features before invoking MetaMake. A fresh configured SDK previously failed
because MetaMake did not exist yet. The regression runs the generated batch
with real GNU make and an initially absent MetaMake, verifies prerequisite
completion and checks the complete runtime target list passed to it.

The core63 qualification completes all 83 public VM runtime targets in983.62s
with unchanged source identities, then packages the SMP/USB/Virtio image.
The windowed two-CPU QEMU TCG run passes in29.02s: concurrent ordinary Exec
tasks with x18/FPSIMD preservation, timer delivery under computation, wakeup,
teardown, secondary shutdown and Virtio graphics IRQ. LLDB attached before
execution and verified registers, memory, stepping and resume/interruption.
Evidence: `artifacts/consolidation-reference-cohort/` and
`artifacts/consolidation-reference-qemu/verdict.json`.

The CPU widget capture confirms two labeled processors; it captures the initial
inventory state, not established utilization percentages. The SMP runner does
not apply the empty-desktop pixel gate (`desktop_checked=false`). This QEMU result alone
does not qualify production VZ, semantic input, apps, GPU workloads or physical
Apple hardware; VZ promotion is supported by the separate current-reference
checks above. LLDB's batch exit1 is from
a backtrace command issued after QEMU's normal exit; the preflight protocol is
recorded independently. Its automatic disassembly at the halted secondary's
zero PC also reports an unmapped read after firmware resumes; the explicit
initial memory read and instruction step succeeded.
