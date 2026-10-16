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
The consolidated candidate is undergoing fresh guest build checks.
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
to build the required PCI/virtio/USB/network components. Fresh full packaging,
real VZ/HVF desktop input, networking, persistent files and clean shutdown still
need acceptance against the pinned consolidated build. Audio and production SMP
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
