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
before destroying it. A resident owner and standard AROS device interface are
separate integration work. Hardware support assumes coherent PCI DMA;
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

Build the launcher with `make build/macaros-vm`, package a current headless
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
integration require a resident stream owner and application services beyond
this transport qualification.
