# Macaros VM

AROS AArch64 running inside a hardware virtual machine on macOS. The guest
uses the native AROS ABI, including x18 as a compiler temporary. The macOS
launcher follows Apple's host ABI.

This private project reuses the AROS kernel, UEFI loader and bundle tools.
The existing hosted Macaros remains in its own repository.

## Build and verify

On Apple Silicon with Xcode command-line tools, Homebrew LLVM/lld, QEMU,
mtools and dtc:

```sh
make
make test
make -C boot/arosboot
```

`make test` executes a real EL1 guest with Hypervisor.framework, checks x18
across VM exits, rejects a corrupted probe, and exercises the core FDT mapper
under ASan/UBSan. `AROS_CORE_SOURCE` selects the kernel checkout; the default
is `../aros-core-vm`.

`tools/prepare-esp.py` reuses a verified native bundle while substituting the
VM bootstrap and kernel. `tools/run-qemu.py --esp DIR --output NEW_DIR` runs
with HVF, records exact inputs and requires both the kernel startup contract
and the Wanderer framebuffer check. `--accel tcg` is the software regression
lane; it never substitutes for an HVF failure.

`build/macaros-vm DISK_IMAGE STATE_DIRECTORY [--headless]` launches the
Virtualization.framework application. The image must be a writable private
copy. Firmware state lives separately in `STATE_DIRECTORY`. The qualified VZ lane
is headless: `tools/run-vz.py --disk IMAGE --output NEW_DIR`. The VZ display
requires a virtio GPU driver. See [guest build instructions](docs/BUILD.md).

See [the handoff](when%20john%20is%20back.md) for measured results and work in
progress, and [provenance](docs/PROVENANCE.md) for reused code.
