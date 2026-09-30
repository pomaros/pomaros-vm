# Sources and ownership

- Architecture: private `jonx/aros-next`, `docs/AARCH64.md`, revision `3b0d902`.
- `boot/arosboot` (except `vz_platform.c`), `boot/abi/bundle.json`, `tools/arosbundle`,
  `tools/boot_generation.py`, `tools/gen-bootinfo.py`, `boot/abi/bootinfo.json`,
  `tests/check-aros-render-frame.py`: MIT code
  reused from `jonx/aros-apple-silicon` revision `7050a59`. The loader is adapted
  for VM boot and writes diagnostic `BOOT.LOG` on its boot volume.
- OS changes: private `jonx/aros-apple-core`, branch `codex-macaros-vm`, based
  on `b30d52e58c`. AROS source retains APL 1.1. It is not copied into this host
  repository.
- Launcher, qualification probes and VM orchestration: MIT, John Knipper.
- Firmware: local QEMU EDK2 file supplied by QEMU's package; never committed.
- Apple APIs: installed macOS SDK headers and
  [VZEFIBootLoader](https://developer.apple.com/documentation/virtualization/vzefibootloader).
- Virtio hardware contracts: [OASIS Virtio 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).

The initial core build reused unchanged native modules from an isolated APFS
clone of the qualified build. Changed kernel objects and the bootstrap were
rebuilt. This is an incremental native build, not a clean OS build.

`include/vm-console.h` is original MIT code, imported with its license into
the private core. Its control handshake follows the OASIS console protocol.
