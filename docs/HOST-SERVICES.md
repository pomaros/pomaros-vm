# Host file services

The launcher option `--host-services` serves files over virtio-vsock port 4051
using `vmbridge.device` in AROS. The host directory is `Shared` beneath the VM
state directory. This endpoint does not listen on the LAN.

`VMExchange HELLO` checks the connection. `VMExchange PUT local-file host-name`
uploads a file; `VMExchange GET host-name local-file` downloads one. Host names
are single UTF-8 components, at most 255 bytes, excluding leading dots, path
separators, colons and control characters. Files are limited to 64 MiB.
Uploads publish only after flushing a temporary file and linking it without
replacing an existing destination. The server pins the directory descriptor
and opens files relative to it without following symbolic links.

The MVHS version 1 frame has a 20-byte little-endian header: magic `MVHS`,
16-bit version, 16-bit operation, 32-bit increasing request ID, 32-bit payload
length and 32-bit status. Payloads are bounded to 65536 bytes. Operations are
HELLO (1), PUT_BEGIN (2), PUT_CHUNK (3), PUT_COMMIT (4), GET_BEGIN (5),
GET_CHUNK (6) and GET_END (7). At most four connections are admitted, including
connections finishing shutdown.
Status values are success (0), invalid request (1), invalid state (2), IO
failure (3), destination exists (4) and unsupported operation (5).

Host socket-pair tests and the complete VZ service gate pass: upload/readback
of 64 KiB, path/overwrite refusal, and the standalone command HELLO after closing
and reopening the device. The separate file-command gate exercises CLI PUT/GET
against an isolated FAT
volume, checks refusal to overwrite a guest destination, and verifies bytes
from the host after two fresh VMs. The [clipboard bridge](CLIPBOARD.md) also passes a real guest-device VZ gate.
Both clients serialize the exclusive guest stream; background polling skips busy
sessions. Desktop auto-start and foreground session contention pass a bounded VZ
clipboard session with Wanderer.
Host power-loss durability and concurrent host modification during download
are not qualified; a download is not a snapshot.

Build the host with `make`; run host tests with `make test-host-services`.
Package the headless PCI guest with `tools/package-core.py --host-services
--pci-hidd --build BUILD --efi EFI --output NEW_ESP`. The EFI path is explicit
to prevent silently selecting an old loader without PCI description. `tools/run-vz.py --require-host-services` requires the protocol marker, successful command
HELLO, two positive device IRQ counters and exact host fixture contents.
A partial transfer result cannot make this gate pass.

Evidence and outstanding diagnosis are recorded in
[`when john is back.md`](../when%20john%20is%20back.md).


For the CLI gate, package with `--pci-hidd --file-commands` instead of
`--host-services`, create the 64 MiB boot disk with `tools/make-disk.py`, then
run `tools/run-filesystem-vz.py --disk DISK --output NEW_RUN --file-exchange`.
This appends a separate FAT test volume. Each phase uploads the 64 KiB fixture,
downloads it through the command and refuses a second download onto the same
name. The restart phase verifies the first file survived and writes a second
file. The runner checks both the host Shared directory and the guest disk image.
Only file-backed test disks are touched. This does not qualify host power loss
or GUI-driven file selection.

## Host-requested shutdown

MVHS v1 adds two operations on the existing serialized guest service connection:

| Op | Request | Response |
|---|---|---|
| 13 | empty | LE u64 shutdown token; zero means no request |
| 14 | token:u64 + result:u32, little endian | empty; result 0 means volume inhibited/flushed, 1 means failure |

The host generates a nonzero token when quit is requested. It accepts only the
current token, rejects unsolicited/stale or contradictory acknowledgements, and
accepts identical repeated acknowledgements. Failed requests may be retried with
a new token. A missing reply never authorizes stopping the machine.

`VMSync VOLUME=VMWORK0:` polls before clipboard work, using the existing shared
service gate. Inhibit flushes the FAT cache and submits CMD_UPDATE. On success
the guest leaves that device inhibited, skips further clipboard synchronization,
and can resend its acknowledgement after transport loss. This prevents later
filesystem writes crossing the acknowledged boundary. No attempt is made to
save application buffers or quiesce additional devices. The VZ/FAT path is
qualified; another filesystem/backend needs its own flush contract verification.

Normal host quit, window close and SIGTERM share Application's asynchronous
shutdown path when host services are enabled. AppKit's immediate termination is
cancelled while VZ continues serving guest connections; after acknowledgement,
VZ stops and the app requests termination again. A flush failure keeps the VM
running; after 15 seconds without acknowledgement it reports that it is still
waiting. `--force-stop` bypasses this protocol deliberately, including in the
existing bounded boot runner. The dedicated shutdown runner never uses it.

Qualification: host socket-pair tests cover idle/invalid/stale tokens, failed
request retry and idempotent/conflicting acknowledgements. The VZ shutdown test
writes/closes 64 KiB without explicit probe Flush/Inhibit, sends SIGTERM after
desktop startup, requires ordered host-request/guest-quiesce/host-ack markers and
normal process exit, then checks exact bytes with mtype. This is a closed-file
persistence test, not proof that the FAT timer had not already flushed the data,
not a host-power-loss test, and not an independently clicked GUI-close test.
