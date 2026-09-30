# Building the guest

The host builds with `make`; the UEFI loader builds with
`make -C boot/arosboot`. `tools/build-core.sh` builds the changed bootstrap and
kernel in an isolated, configured AROS build. It deliberately selects explicit
metatargets to reuse the existing cross toolchain.

The initial local setup uses:

- core source: `~/Source/aros-core-vm`, branch `codex-macaros-vm` in the private
  `jonx/aros-apple-core` repository, qualified revision `1669b1a090`;
- configured build: `~/Build/macaros-vm/core`, APFS clone of the qualified
  native build, with generated include flags redirected to this checkout;
- guest C/assembly target: AArch64 ELF, no `-ffixed-x18`;
- EFI target: AArch64 PE/COFF, firmware calling convention;
- host target: macOS arm64, Apple's calling convention.

A copied generated `arch/aarch64-apple/mmakefile` must match the guest source's
bootstrap flags, including the explicit `AROS_VM_GUEST=1` profile. The build
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
