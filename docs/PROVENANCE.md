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

## Portable early boot

The private core owns `arch/aarch64-all/boot/fdt` and `arch/aarch64-all/irq`.
The reader and GIC adapter are APL 1.1. The former contains libfdt v1.7.2,
commit `2d10aa2afe35527728db30b35ec491ecb6959e5c`, selected under its BSD-2-Clause
option; notices and the BSD license accompany the files. The EFI build links
that same source copy with the freestanding environment. The executable is a
combination of MIT, APL and BSD components, not wholly MIT.

GICv2 register operations reuse the AROS BCM2711 implementation; GICv3 follows
[Arm's GICv3/v4 overview](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/GICv3_v4_overview.pdf)
and the GIC architecture specification IHI 0069. Devicetree interpretation follows
[DTSpec](https://devicetree-specification.readthedocs.io/en/stable/devicetree-basics.html).
Linux binding documents supply data-format facts; no Linux driver code is copied.
