# Building the guest

The host builds with `make`; the UEFI loader builds with
`make -C boot/arosboot`. `tools/build-core.sh` builds the changed bootstrap and
kernel in an isolated, configured AROS build. It deliberately selects explicit
metatargets to reuse the existing cross toolchain.

The initial local setup uses:

- core source: `~/Source/aros-core-vm`, branch `codex-macaros-vm` in the private
  `jonx/aros-apple-core` repository, qualified revision recorded in `when john is back.md`;
- configured build: `~/Build/macaros-vm/core`, APFS clone of the qualified
  native build, with generated include flags redirected to this checkout;
- guest C/assembly target: AArch64 ELF, no `-ffixed-x18`;
- EFI target: AArch64 PE/COFF, firmware calling convention;
- host target: macOS arm64, Apple's calling convention.

A copied generated `arch/aarch64-apple/mmakefile` must match the guest source's
bootstrap flags, including the explicit `AROS_VM_GUEST=1` profile. The generated
`arch/aarch64-apple/kernel/mmakefile` must also include its conditional
`USER_CPPFLAGS += -DAROS_VM_GUEST=1`; the helper refuses a stale kernel makefile.
This selects task-owned x18 in the scheduler and the three-task canary proof.
The build
helper selects it; the default physical Apple profile still reserves x18. An out-of-tree
source shadow or an include flag pointing to a different checkout is invalid.

Prepare an ESP from a verified bundle and the rebuilt guest artifacts:

```sh
python3 tools/prepare-esp.py --base BASE.BND \
  --stage2 "$AROS_BUILD/bin/apple-aarch64/gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-test.bin" \
  --kernel "$AROS_BUILD/bin/apple-aarch64/AROS/boot/apple/Devs/kernel.resource" \
  --output build/esp
python3 tools/make-disk.py --esp build/esp --output build/system.img
python3 tools/run-vz.py --disk build/system.img --output artifacts/vz-run
```

Use the 22-module core qualification bundle for the headless VZ lane. Use the
64-module Wanderer bundle for the QEMU framebuffer lane. If using the
AFS qualification seed `/tmp/afs-review-corrected-bundle/AROS-qemu.BND`, pass
`--exclude afsplus-handler`: that extra module is outside this startup workload
and the 65-module combination fails the stage-two startup gate. These are different
qualification workloads. Input hashes are recorded with each prepared ESP.
The named output directories and disk images must be new.

The MIT early-console protocol header is authored in `include/vm-console.h`;
its imported copy in the APL core is `include/aros/apple/vmconsole.h`. Keep
them identical when updating the protocol. The boot allocation schema adds
`AROS_BOOT_ALLOCATION_EARLY_CONSOLE` for a single retained 4-KiB record; older
consumers reject that allocation. Pin the matching core with each image.

## Portable platform probes

```sh
python3 tools/qualify-platform.py --output artifacts/platform-NEW
```

The default firmware corpus is `../aros-raspi/payloads/pi4/bcm2711-rpi-4-b.dtb`;
use `--rpi4-dtb` to select another copy. The runner copies it and selects the
modeled PL011 console in that copy. It records the original hash and override.
The same guest binary runs on QEMU virt/HVF and raspi4b/TCG, with the FDT captured
from guest RAM and compared with dtc/fdtget. Timer and register-restoration
negative controls retain independent QMP register evidence on failure.

These probes do not boot the whole AROS system or access a physical Pi.
The VZ startup and HVF Wanderer checks remain separate acceptance runs.

Host parser corpus (ASan/UBSan):

```sh
python3 tests/early-fdt.py --tree PATH.dtb --output artifacts/fdt-corpus.json
```

`make test` includes the synthetic bus/alias/interrupt fixture. Each corpus
input is checked at every truncated length and with deterministic mutations.
The core and EFI builders share one libfdt import. A configured guest build
must regenerate the platform makefile after build-rule changes, or mirror its
new shared-FDT include paths and objects in the isolated generated makefile.

## Kernel deadline timer qualification

`tools/run-qemu.py --require-timer --gic-version 3 --esp ESP --output NEW`
requires three real `timer.device` requests to complete and at least three
kernel timer firings, in addition to startup and the Wanderer pixel check.
Repeat with `--gic-version 2`. Failed runs stop the CPU and save QMP registers
before terminating the process. The optional `--dtb` input is copied into the
run artifact directory.

For the negative control, build an isolated negative kernel with the sole
change in `vm_irq.c` from `AROSGICEnable(&controller, timer_id, trigger)` to
`AROSGICEnable(&controller, timer_id ^ 2U, trigger)`. Package that kernel into
a separate ESP, then restore and rebuild the positive source. Verify its hash
matches the original positive kernel. Run:

```sh
python3 tests/kernel-timer-control.py --esp POSITIVE_ESP \
  --negative-esp NEGATIVE_ESP --output artifacts/kernel-timer-NEW
```

The negative must reach GIC initialization, time out before the wait proof,
and preserve CPU registers. Changing the timer IRQ in QEMU's firmware FDT is
not an equivalent negative: EFI consumes that description too and can block
before AROS starts.

The kernel generated makefile must include `vm_irq` and the conditional shared
GIC/libfdt objects and compilation rules from its source template. As with the
bootstrap rules, refresh it after pulling build changes. VZ startup qualification requires GIC readiness and the three-wait timer
proof. Use the matching EFI loader and kernel; the generated FDT includes
named timer interrupts.

The isolated build helper also removes inherited `-ffixed-x18` from generated
`target.cfg` before compiling. This corrects the native configuration copied
with the original APFS build. It does not retroactively rebuild seed bundle
modules or every previously compiled object: replacing all such artifacts is
part of the clean full guest build. Newly rebuilt VM code must not reserve x18.

Stage two passes four scalar GIC window bounds in protected boot tags. The
kernel requires all four, with no duplicates, and matches both FDT register
windows exactly before GIC MMIO access. Old stage-two binaries without these
tags fail the VM IRQ contract; package matching stage two and kernel binaries.
`make test` checks tag emission, capacity, incomplete records, duplicates and
overlap under ASan/UBSan.

## Kernel device-handler chain qualification

VZ requires `[VM] IRQ handler chain PASS`. With QEMU, add
`--require-irq-chain --require-timer`, and exercise both GIC versions. The proof
uses a software-pended SPI and the public handler APIs, including rejection of
unconfigured/reserved sources, unsupported polarity, cyclic attribute lists,
and reconfiguration while handlers are installed. It validates callback data
and final-removal hardware masking; it is not a real peripheral or SMP test.
The kernel generated makefile must include the `modifyirq` architecture override.

For a dispatch negative control, replace only the call to `krnRunIRQHandlers`
in the isolated VM adapter with a no-op, build and package a separate ESP.
The guest must pass its timer waits and then print `[VM] FAIL: IRQ handler chain`;
the runner must reject the boot. Restore the source, rebuild and compare the
positive kernel hash before using the build again.

## Fresh configuration

```sh
python3 tools/configure-core.py --output "$HOME/Build/macaros-vm/clean-core"
make -C "$HOME/Build/macaros-vm/clean-core" -j8 apple-boot-payloads
```

The output directory must not exist. The helper uses the prebuilt cross
toolchain in `~/aros-crosstools` (override with `--toolchain`), records the
configure command and log, and selects `--enable-vm-guest`. This profile exports
`AROS_VM_GUEST=1` to generated makefiles and omits the native x18 reservation
from C and C++ flags. Other targets cannot select this profile; the physical
Apple default retains its existing flags. This creates no copy of native
object files. The full build and packaging acceptance state belongs in the
handoff log, separate from successful configuration.
