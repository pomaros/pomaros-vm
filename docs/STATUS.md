# Qualification status

PomAROS VM is under preparation. The product's host application and shared
packaging tools build; a fresh full desktop release from the consolidated core
is still being qualified. A running VM process does not establish a booted or
usable desktop.

## Migration evidence — 2026-10-16

The current product source uses PomAROS branding, workspace-relative paths and
a shared `../aros-core-pomaros` OS checkout. The application uses fresh profiles
under `~/Library/Application Support/PomAROS VM/`; no existing personal profile
was opened, changed or removed during migration. MIT/APL/BSD provenance and Git
history are retained.

The Swift launcher builds and is ad-hoc signed. Host service framing, clipboard
codec/services, VM storage safety, desktop packaging admission, backend admission
and source-pin tests pass. The QEMU backend deliberately has no VZ host-service
provider or coordinated VMSync quit participation.

Against common core candidate `f32aa81736`, the EFI loader builds, five C
sanitizer binaries and five Python parser/codegen/allocator gates pass. Generated
boot ABI headers and the console protocol header match. The allocator fixture
was adapted to the common core's IRQ-save/restore locking API; no OS code was
changed for that fixture. Evidence is in ignored
`artifacts/pomaros-migration/common-host-tests.json` and adjacent logs.

The supported configure helper passes on a fresh default build directory at
locked candidate `b027ee4ecd`, with source epoch `1792165783`, UTC and unrestricted
guest x18. Evidence: `artifacts/pomaros-migration/pinned-configure.log`.
The candidate's `apple-wanderer-payloads` build completes with exit 0. Its complete distribution graph, base commands and explicit CPUInfo target build
successfully. Fresh VZ and QEMU desktop packages contain respectively 3866 and
3863 runtime files; the QEMU package omits the VZ bridge services. Both windowed
applications package and sign successfully. This establishes build/package
completion, with desktop runtime acceptance tracked separately.

A fresh headless core bundle from that build passes real QEMU/HVF (7.85 s)
and Virtualization.framework (2.36 s) runs, both in normal host windows. The
guest probe remains headless. Both runs pass DOS takeover, portable ABI,
emulated TLS, streams, W^X/unload, timer waits, IRQ chain and retained FDT/PCI
checks. Their guest bundle is byte-identical:
`6abba4b73af8c513404ff88b633498c39b2db630e3f9bbb207fb3aba734ca829`.
QMP register access is proved while paused before the HVF workload. VZ has no
public independent guest-register API; its kernel serial context and host/view
capture are retained on failure. Both VMs stopped and their reservations were
released. Evidence: `artifacts/common-core-regression.json`, `artifacts/core-hvf/`
and `artifacts/core-vz/`. These checks establish core boot/ABI rather than
desktop, network, persistent files or safe window-close acceptance.
Each shipped package must identify its actual core revision and inputs; previous
candidate host checks do not establish guest acceptance for that revision.

## Open guest acceptance

The earlier clean VM audit built the kernel and Wanderer, but full distribution
building failed on QuickPart's missing UUID header. Its existing integrated
application also crashed in `InternalLoadSeg_ELF` after file-exchange startup.
The invalid segment-chain pointer was localized; the original cause remains
unproven. The common core includes a QuickPart dependency correction that needs
fresh build verification.

A QEMU desktop packaging attempt from the partial audit build refused missing
`pci.hidd`. The shared distribution dependency graph has since been expanded
to build the required PCI/virtio/USB/network components and base commands.
The earlier partial audit had built `workbench-c` separately; the canonical
distribution graph explicitly includes it so clean builds do not inherit those
commands from an earlier step. The fresh QEMU/HVF desktop run aborts inside QEMU 11.0.1's HVF MMIO handling
(`assert(isv)`) immediately after xHCI instantiation. Its host crash report and
full logs are retained; the guest instruction remains under debugger diagnosis.
A single TCG comparison reaches desktop stability, graphics IRQ and two USB
bindings, but the strict pixel gate rejects the visible debug statistics window
as requester geometry. Its overall verdict remains FAIL. Evidence:
`../pomaros-qemu/artifacts/desktop-hvf/` and `artifacts/desktop-tcg/` in that repo.
The supervised HVF failure captures guest PC `0x538604f0` through a successful
Hypervisor register read on the owning CPU thread. Its final MMIO abort has
VA/IPA `0x8000014004`, ISV=0 and read direction. Evidence is in
`../pomaros-qemu/artifacts/desktop-hvf-lldb/`.

Fresh windowed VZ stage6 passes runtime transport-cut recovery, file exchange,
JPEG decoding with nonflat guest pixels, desktop/HID/GPU markers and the
window-close quiesce acknowledgement. The overall check remains FAIL: after a
clean process exit, FAT16's clean-shutdown bit is clear and normal mtools refuses
the disk. Both FAT copies agree; read-only diagnostic access verifies both
persisted file fixtures exactly, so filesystem corruption is not established.
Evidence: `artifacts/stage6-fresh/verdict.json` and
`artifacts/stage6-fresh/post-exit-fat-diagnostic.json`.
Real input events, networking and fully clean persistent shutdown still need
acceptance against the pinned consolidated build. Audio and production SMP
are not qualified by this migration.

No upstream publication or visibility change is part of these checks. The
native product repository stays empty/private. Native Apple and Raspberry Pi
hardware have not been exercised by this migration.

## Development record

The earlier long cross-project development journal is preserved in private Git
history and the original source checkout; it is excluded from the current
product tree. Record new product milestones here with exact source identities,
actual commands, verdicts and artifact references. Build and run procedures live
in [BUILD.md](BUILD.md); architecture lives in [PORTABILITY.md](PORTABILITY.md).

The follow-up supervisor capture reads the actual instruction from guest RAM
using the host mapping observed at `hv_vm_map`. Guest PC `0x538604f0` contains
`0xb8404d69`, decoded as `ldr w9, [x11, #4]!`; guest x11 and x26 both equal
`0x8000014000`. All three Hypervisor register reads return success on the
owning CPU thread. This proves the scalar writeback MMIO instruction rather
than a SIMD hypothesis. Evidence: `artifacts/desktop-hvf-opcode/` in the QEMU
repository. Core correction and fresh runtime acceptance remain pending.

Production packaging selects the strict `desktop` session policy, requesting an
endless guest session and `aros,desktop-hide-shell=1`. Its PomAROS monitor widget
remains visible with github.com/pomaros and github.com/jonx. Diagnostic `forever`
retains its Shell. Firmware FDT and package admission tests pass; the
policy requires the coordinated common-core hook before runtime acceptance.
