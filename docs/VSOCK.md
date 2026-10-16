# Host services transport

The VM plan in `aros-next/docs/AARCH64.md` selects virtio-vsock for optional
host services. It carries a stream between guest and host without depending
on the Wi-Fi interface, NAT or a host TCP listener. Native platforms need a
separate network binding for the same application services.

The APL core `rom/devs/virtio/vsock_transport.c` reuses the shared PCI mapping,
DMA allocator, split queues and acknowledged reset. Its wire contract is
[OASIS Virtio 1.2, section 5.10](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html#x1-43600010).
The transport exposes bounded packet send/receive, CID discovery and optional
interrupt registration. Three queues cover receive, transmit and reset events.
One task owns the context; packets are copied into owned DMA buffers. Receive
continues while transmit is pending. A failed reset retains DMA and callback
ownership; its caller must remain resident. A transport event invalidates the
context: close and reopen before reconnecting with the refreshed CID.

The packet API is wrapped by `vsock_stream.c`, a portable state engine for one
outgoing stream. It owns fixed 16 KiB transmit/receive rings and performs
fragmentation, peer credit validation, consumed-byte accounting, partial reads
and writes, and shutdown/reset. An emit callback advances state only after the
transport accepts a copied packet. Data packets carry updated credits; RX
progress does not depend on TX availability. Counters wrap modulo 32 bits and
cannot acknowledge unsent bytes. Failure stays observable after reset emission.

The engine has no OS dependencies or worker thread. Its owner must schedule
packet input/output, bound connect/close deadlines, retain and reject unrelated
flows, allocate fresh ports for reconnects and discard streams on transport
reset. A closed stream may retain received bytes: applications must drain them
before destroying it. The resident `vmbridge.device` supplies scheduling and an Exec device interface
for one exclusive host stream. Hardware support assumes coherent PCI DMA;
physical-platform cache and IOMMU contracts need separate qualification.

## Bounded stream qualification

`make test-virtio-vsock` runs the actual packet transport under ASan/UBSan with
owned-buffer, RX-during-TX, ring wrap, malformed completion and reset cases.
`make test-vsock-stream` checks credits, bounded rings, fragmentation, blocked
TX, counter wrap, malformed packets, shutdown and draining after closure.
The PCI guest probe recognizes device 1af4:1053 and establishes two successive
streams to host CID 2, port 4050, using distinct local ports. Each transfers
64 KiB and compares the complete echo byte for byte through 997-byte application
reads/writes and 16 KiB buffers. The flow engine handles credit updates and
closure. The probe resets the device before returning. It uses bounded polling;
it does not qualify IRQ completion or automatic recovery after transport reset.

The launcher's opt-in `--vsock-probe` adds Apple's socket device and a listener
on port 4050. The listener accepts at most two connections, handles partial
stream IO on a worker queue, echoes exactly 64 KiB per connection within a
monotonic ten-second deadline, and closes. It exposes neither host files nor
the clipboard. VZ owns each connection file descriptor; teardown runs on the
main queue.

Build the launcher with `make build/pomaros-vm`, package a current headless
image with `tools/package-core.py --pci-hidd`, then run:

```sh
python3 tools/run-vz.py --disk IMAGE --output NEW_DIR \
  --require-pci-hidd --require-vsock --timeout 30
```

The runner requires one guest two-stream marker, exactly two host echo
markers, and the ordinary full startup contract. `tools/vz_contract.py` shares
these exact counts with `tests/vsock-contract.py`, which replays captured logs
with missing/duplicate-marker controls without rewriting a VM verdict. Results live in `verdict.json`; `vsock_stream_verified` is
true only when the whole run passes. Files, clipboard text/images and desktop
integration require application services beyond this transport qualification.

## Resident Exec device

The APL core target `workbench-devs-vmbridge` builds `Devs/vmbridge.device`.
`OpenDevice("vmbridge.device", host_port, request, 0)` opens one exclusive stream
to host CID 2. Allocate a full IOStdReq. Nonzero flags and concurrent opens are
rejected. The worker owns the PCI device, DMA, IRQ callback and stream. Open waits
for the handshake with a five-second deadline. Each reopen uses a fresh local
port; transport-reset recovery requires close/reopen.

- `CMD_READ` returns available bytes, possibly fewer than requested; zero means
  EOF after received bytes are drained.
- `CMD_WRITE` copies as much as the bounded transmit ring accepts. The caller
  advances by `io_Actual` and resubmits the remainder. Completion means accepted
  locally, not remotely committed to storage.
- `CMD_UPDATE` waits for queued bytes and the device's outstanding TX packet to
  drain. File services must additionally implement application acknowledgments.
- `AbortIO` cancels queued work or marks an active copy for completion with
  IOERR_ABORTED. Caller memory remains live until WaitIO completes.
- Close rejects new IO, cancels pending requests, attempts stream drain and
  shutdown for up to three watchdog ticks, then resets the device. A failed
  reset pins DMA, callback, task, provider and module rather than freeing them.

IRQ callbacks acknowledge and signal only. A one-second watchdog bounds startup
and close and provides fallback wakeups. The worker drains RX independently of
TX and retains one unrelated-flow RST while TX is busy. The ownership handshake
and AbortIO critical sections are uniprocessor contracts. This does not claim
SMP safety or hot removal.

Build and package:

```sh
make -C BUILD -j4 workbench-devs-vmbridge
python3 tools/package-core.py --build BUILD --output NEW_ESP \
  --pci-hidd --vsock-device --efi EFI
python3 tools/make-disk.py --esp NEW_ESP --output NEW_IMAGE
python3 tools/run-vz.py --disk NEW_IMAGE --output NEW_RUN \
  --require-pci-hidd --require-vsock-device --timeout 30
```

When the device is packaged, the PCI probe leaves vsock ownership to the device.
The device probe runs two full Open/Close cycles, each with an aborted pending
read, 64 KiB write/UPDATE/readback and byte comparison. The runner also requires
a positive IRQ count on each close. It sets `vsock_device_verified` and
`vsock_device_irq_verified` only on overall success. Fault-injected reset
quarantine, unrelated-flow reply pressure and close deadline exhaustion remain
unqualified; sustained desktop use and the application services are separate.
