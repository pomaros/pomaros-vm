# Host services transport

The VM plan in `aros-next/docs/AARCH64.md` selects virtio-vsock for optional
host services. It carries a stream between guest and host without depending
on the Wi-Fi interface, NAT or a host TCP listener. Native platforms need a
separate network binding for the same application services.

The APL core `rom/devs/virtio/vsock_transport.c` reuses the shared PCI mapping,
DMA allocator, split queues and acknowledged reset. Its wire contract is
[OASIS Virtio 1.2, section 5.10](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html#x1-35800010).
The transport exposes bounded packet send/receive, CID discovery and optional
interrupt registration. Three queues cover receive, transmit and reset events.
One task owns the context; packets are copied into owned DMA buffers. Receive
continues while transmit is pending. A failed reset retains DMA and callback
ownership; its caller must remain resident. A transport event invalidates the
context: close and reopen before reconnecting with the refreshed CID.

This API is a packet transport, not a full socket implementation. Its caller
must implement stream connection state, credit accounting, buffering, reset
responses for unsupported types, shutdown and reconnection. Hardware support
assumes coherent PCI DMA. Physical-platform cache and IOMMU contracts need
separate qualification.

## Bounded stream qualification

`make test-virtio-vsock` runs the actual packet transport under ASan/UBSan with
owned-buffer, RX-during-TX, ring wrap, malformed completion and reset cases.
The PCI guest probe recognizes device 1af4:1053 and establishes one stream to
host CID 2, port 4050. It advertises 32 bytes of receive credit, respects the
initial peer credit, sends 32 bytes tied to the guest counter and compares the
complete reply across packet boundaries. It resets the flow and device before
returning. The probe uses bounded polling; it does not qualify IRQ completion.

The launcher's opt-in `--vsock-probe` adds Apple's socket device and a listener
on port 4050. The listener accepts one connection, handles partial stream IO
on a worker queue, echoes exactly 32 bytes within five seconds and closes.
It exposes neither host files nor the clipboard. VZ objects own the connection
file descriptor; teardown runs back on the main queue.

Build the launcher with `make build/macaros-vm`, package a current headless
image with `tools/package-core.py --pci-hidd`, then run:

```sh
python3 tools/run-vz.py --disk IMAGE --output NEW_DIR \
  --require-pci-hidd --require-vsock --timeout 20
```

The runner requires guest and host exchange markers plus the ordinary full
startup contract. Results live in `verdict.json`; `vsock_stream_verified` is
true only when the whole run passes. Files, clipboard text/images and desktop
integration require a resident stream owner and application services beyond
this transport qualification.
