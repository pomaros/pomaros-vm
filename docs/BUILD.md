# Building the guest

The host builds with `make`; the UEFI loader builds with
`make -C boot/arosboot`. `tools/build-core.sh` builds the changed bootstrap and
kernel in an isolated, configured AROS build. It deliberately selects explicit
metatargets to reuse the existing cross toolchain.

The supported configuration builds into a new directory with
`tools/configure-core.py`, followed by the payload target and
`tools/package-core.py` as shown below. The packager consumes only products
from that build and refuses missing components. No native object directory or
seed bundle is needed. Core source lives in `~/Source/aros-core-vm`, on the
private `codex-macaros-vm` branch; qualified revisions are recorded in
`when john is back.md`.

Guest C and assembly target AArch64 ELF without `-ffixed-x18`. EFI targets
AArch64 PE/COFF with the firmware calling convention; host code follows the
macOS ABI. The legacy `tools/prepare-esp.py --base` helper is for replacing
individual artifacts during diagnostic experiments, not a complete rebuild.
Use separate, newly named ESP directories and disk images for each run.

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
make -C boot/arosboot BUILD=../../build/uefi-clean
python3 tools/package-core.py --build "$HOME/Build/macaros-vm/clean-core" \
  --efi build/uefi-clean/BOOTAA64.EFI --output build/fresh-headless
python3 tools/make-disk.py --esp build/fresh-headless --output build/fresh-headless.img
python3 tools/run-vz.py --disk build/fresh-headless.img --output artifacts/fresh-vz
python3 tools/run-qemu.py --esp build/fresh-headless --profile core \
  --require-timer --require-irq-chain --output artifacts/fresh-hvf
```

The output directory must not exist. The helper uses the prebuilt cross
toolchain in `~/aros-crosstools` (override with `--toolchain`), records the
configure command and log, and selects `--enable-vm-guest`. This profile exports
`AROS_VM_GUEST=1` to generated makefiles and omits the native x18 reservation
from C and C++ flags. Other targets cannot select this profile; the physical
Apple default retains its existing flags. This creates no copy of native
object files. It initializes the four pinned catalog submodules required by
DOS, ASL, muimaster and Wanderer before configuring.

The packager consumes ordered paths in `boot/headless-modules.json` or
`boot/wanderer-modules.json`,
builds the ABI command with the fresh SDK, creates the deterministic SYS image,
and records source revision and input hashes. It refuses missing products and
a dirty core source; there is no seed-bundle fallback. `--profile core` on the
QEMU runner checks startup and requested IRQ/timer proofs without claiming a
desktop. The default `wanderer` profile retains the desktop pixel gate.
Repeating packaging can verify byte-identical bundles; that is separate from
comparing two independent complete builds. Build and package the desktop with:

```sh
make -C "$HOME/Build/macaros-vm/clean-core" -j8 apple-wanderer-payloads
python3 tools/package-core.py --build "$HOME/Build/macaros-vm/clean-core" \
  --efi build/uefi-clean/BOOTAA64.EFI --profile wanderer --output build/fresh-wanderer
python3 tools/run-qemu.py --esp build/fresh-wanderer --output artifacts/fresh-desktop \
  --require-timer --require-irq-chain --gic-version 3
```

This uses QEMU's framebuffer. It does not qualify the VZ virtio GPU or input
devices. The desktop's source and binary identities are in package provenance;
qualification results are recorded in the handoff log.

## Supervising the early virtio console

Run the VZ firmware bridge and the same guest console transport under QEMU/HVF:

```sh
python3 tools/run-qemu.py --esp ESP --profile core --console virtio \
  --require-timer --require-irq-chain --output artifacts/virtio-console-NEW
```

This selects ACPI, GICv3 and a modern PCI virtio console. Guest output is in
`serial.log`; PL011 firmware output is retained separately in
`firmware-serial.log`. On failure the runner stops the guest and captures CPU
registers through QMP, independently of virtio. A deliberately short timeout
checks this capture path but is not a guest fault reproduction. The ordinary
framebuffer lane retains `--console pl011` and firmware FDT discovery.
