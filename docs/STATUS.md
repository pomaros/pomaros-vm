# Qualification status

PomAROS VM builds and boots its full AROS desktop using Apple's Virtualization.framework. The README image is an unmodified capture of the actual windowed application, including its title bar and the PomAROS monitor with both project URLs. It uses an isolated disk; personal profiles were not opened or changed.

## Verified on 2026-10-16

The shared core pin is `4613340d3f1932b156420d86e3074372e5d15501`. The guest SDK was built fresh with frozen LLVM 20.1.0, using release epoch `1792169063` and UTC. The full distribution graph and all 44 required-file admissions pass. Both backend distributions and their signed development applications package successfully.

Actual guest objects were compiled at `48e445615380448d74ec02c9958a71201ce4904c`. Subsequent shared-source changes add a hosted-only ELF protection correction, extract the unchanged monitor renderer into a shared header, and add a Darwin-only SystemMonitor utility. The standalone loader tokens and the entire guest coldstart translation unit retain identical preprocessed code. Qualified artifacts and runs keep their original source metadata at `3c382bc`; this source-pin update does not relabel those runs or rebuild unchanged guest objects. No earlier compiler's guest objects were imported.

The final VZ stage6 run passes in 18.6 seconds: forced vsock interruption, guest session reopening, file PUT/GET, clipboard reconnection, MultiView JPEG decoding with varied pixels, desktop stability, two USB HID bindings and GPU IRQ completion. One window-close request receives the guest quiesce acknowledgement and exits normally. Normal mtools reads both persisted file fixtures; the two FAT copies agree and the FAT16 clean-shutdown bit is set. The immutable seed remains unchanged. Local evidence: `artifacts/stage6-final-3c/`.

A separate production desktop run injects no guest probe, captures its real window through the public current-process ScreenCaptureKit API, and closes through the same quiesce path. It passes in 8.99 seconds. This produces `docs/images/pomaros-vm.png`; local evidence: `artifacts/desktop-production-final-3c/`. Capturing one's own window requires macOS 14.4 or later and does not request global screen access. Normal VM operation retains its macOS 13 requirement.

Swift host/service/storage checks, firmware FDT sanitizer checks, generated protocol consistency and packaging/source/backend admission checks pass. The xHCI capability and port register accesses were checked in actual compiled objects before the successful HVF desktop runs.

## One guest kernel, two backends

Actual final VZ and QEMU bundles were parsed and their payload hashes independently verified. `kernel.resource`, `exec.library` and the raw stage2 bootstrap are byte-identical. Only the `SYS` ramdisk component differs, reflecting backend startup and host-service configuration. The kernel ELF SHA-256 is `646c209ca0cc241d7286522df40f7cafbbcf334a20c8d6574e8d45d7463438a7`. Local evidence: `artifacts/pomaros-migration/final-3c-shared-kernel-proof.json`.

The macOS launcher uses Apple's ABI. The VM guest leaves x18 unrestricted; only the hosted target reserves it. Native Apple and future Raspberry Pi share the ordinary guest register policy and common source, but their hardware is not qualified here.

## Limits

These bounded checks do not establish long-term desktop stability, real keyboard/pointer event delivery, full networking, audio or production SMP. QEMU has no VZ clipboard/file provider or coordinated guest flush/quit transport; its separate status documents that limitation. The earlier failed HVF/MMIO, screenshot and FAT runs remain preserved in local artifacts; their failure verdicts were not rewritten.

The product repositories remain private. The native product repository remains empty and private. Build procedures live in [BUILD.md](BUILD.md) and [DISTRIBUTION.md](DISTRIBUTION.md); architecture lives in [PORTABILITY.md](PORTABILITY.md).
