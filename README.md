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

`tools/configure-core.py` configures an independent VM guest build.
`tools/package-core.py` packages its freshly built modules and records their
source revision and hashes. See the [complete build procedure](docs/BUILD.md).
`tools/run-qemu.py --esp DIR --output NEW_DIR` runs
with HVF, records exact inputs and requires both the kernel startup contract
and the Wanderer framebuffer check. `--accel tcg` is the software regression
lane; it never substitutes for an HVF failure.

`build/macaros-vm DISK_IMAGE STATE_DIRECTORY [--headless]` launches the
Virtualization.framework application. The image must be a writable private
copy. Firmware state lives separately in `STATE_DIRECTORY`. The qualified VZ lane
is headless: `tools/run-vz.py --disk IMAGE --output NEW_DIR`. The VZ display
requires a virtio GPU driver. See [guest build instructions](docs/BUILD.md).

The [host services transport](docs/VSOCK.md) documents the vsock packet API
and bounded stream qualification.

See [the handoff](when%20john%20is%20back.md) for measured results and work in
progress, [portable architecture](docs/PORTABILITY.md), and [provenance](docs/PROVENANCE.md) for reused code.


The optional [host file service](docs/HOST-SERVICES.md) exposes the VM state's
`Shared` directory over virtio-vsock (`--host-services`). Its VZ qualification
covers 64 KiB upload/readback and the standalone `VMExchange HELLO` command;
CLI file operations and automatic clipboard integration are tracked in
[stage 6](docs/STAGE6.md).
