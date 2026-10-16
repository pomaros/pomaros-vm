# VM clipboard bridge

The host service transports the native AROS clipboard's IFF payload. Host
conversion is separate from the stream transport and guest clipboard device;
the protocol can be carried by another transport for physical machines.

`VMSync` bridges clipboard unit 0 to the host service. The VZ launcher supplies
its general pasteboard with `--host-services`; `--clipboard-test` substitutes a
private named pasteboard for qualification. The guest engine passes VZ tests
with the real clipboard device. Desktop auto-start also passes a bounded VZ session with Wanderer and USB/HID.

## Data and conversion

Text uses FORM FTXT with a lossless UTF8 chunk and a CHRS Latin-1 fallback.
The decoder prefers UTF8; applications that only understand CHRS see `?` for
characters outside Latin-1. Multiple text chunks and odd-byte padding are
handled. CSET without UTF8 is rejected.

Images use ILBM: indexed 1–8-plane or 24-plane RGB, optional mask or indexed
transparent color, uncompressed or ByteRun1 rows. Extra-halfbrite palettes are
supported; HAM, DEST/CLUT and dynamic palettes are rejected. The Mac exposes
images as PNG. Partial alpha, oriented images and multi-frame images are
rejected rather than silently converted. Maximum payload is 16 MiB; images
are limited to 4096 pixels per dimension and 4 megapixels. These limits and
unsupported formats remain visible integration constraints.

Format references: [FTXT](https://wiki.amigaos.net/wiki/FTXT_IFF_Formatted_Text),
[UTF8 chunks](https://wiki.amigaos.net/wiki/UTF8_IFF_UTF-8_Unicode_Text), and
[ILBM](https://wiki.amigaos.net/wiki/ILBM_IFF_Interleaved_Bitmap).
Implementation is original MIT code; no third-party implementation was copied.

## MVHS operations

All integer fields below are little-endian. The existing MVHS frame header,
64 KiB frame limit, request IDs and IO deadlines apply. Clipboard operations
share the endpoint on port 4051 with the [file service](HOST-SERVICES.md).

| Op | Request | Successful response |
|---|---|---|
| 8 snapshot | Last-seen revision, 8 bytes | Revision (8), payload length (4); zero length means unchanged |
| 9 snapshot chunk | Requested length (4), 1..65536 | Up to that many bytes from the captured snapshot |
| 10 upload begin | Expected revision (8), total IFF bytes (4) | Empty |
| 11 upload chunk | Nonempty payload bytes | Empty |
| 12 publish | Empty | New revision (8) |

Use an all-ones revision for the first snapshot. Snapshot data stays immutable
for the connection until consumed or replaced by another snapshot. Uploads are
bounded and discarded on disconnect. Publication checks completeness, decodes
the entire payload and compares the expected revision before touching the Mac
pasteboard. Status 6 means the pasteboard changed; a failed conditional publish
returns its current revision (8 bytes). Unsupported content returns status 5.
NSPasteboard has no cross-process compare-and-swap, so this revision check
reduces stale writes but cannot exclude a simultaneous write by another Mac app.

The host admits at most four connections, including ones finishing shutdown.
Each owns its file and clipboard transfer state. Pasteboard calls run on the
main queue. The guest device supports one open stream. VMExchange and VMSync share a named
Exec semaphore with reference-counted storage independent of command segments.
File commands wait for the session; background polls skip a busy session.
Registry lifetime relies on the single-CPU Exec contract; it is not an SMP
implementation. Forced process removal while owning the semaphore is not
recoverable by this mechanism.

## Verification

`make test-clipboard-codec` checks Unicode/Latin-1, bounded IFF parsing, ILBM
planes/masks/padding/ByteRun1, PNG conversion, named-pasteboard publication and
stale/malformed-write refusal. `make test-clipboard-services` exercises actual
socket framing with a multi-frame Unicode clipboard, image publication,
revision conflicts, size rejection and abandoned uploads. It also cuts streams
inside headers/bodies and before publication, reconnects to the same server,
checks transfer-state isolation, and loses a publication acknowledgement. A new
connection recovers the committed value, refuses the stale retry, then completes
a fresh upload. These are host socket-pair tests, not forced VZ/guest recovery. Both use fresh named
pasteboards and leave the user's general clipboard untouched.

The guest gate uses `tools/package-core.py --pci-hidd --clipboard-sync`, then
`tools/make-disk.py` and `tools/run-filesystem-vz.py --clipboard-sync`. Two fresh
VZ boots exercise clipboard.device, the standalone `VMSync ONCE` import,
text/image transfers through the shared synchronization engine, host-wins
conflicts, unchanged revisions, recursive session refusal/reopen, and the file
CLI/persistence gate. Each requires 19 positive bridge IRQ closure counters and
exact markers; the host pasteboard seed and guest/file fixtures are checked.
The image fixture is a two-color ILBM; broader format coverage belongs to the
host codec tests. The separate desktop gate verifies the actual background process and a
foreground session; neither gate proves GUI menu/keyboard paste, forced
transport failure recovery, or prolonged reliability.

## Guest operation

Build `workbench-tools-vmexchange`, `workbench-tools-vmsync` and
`workbench-devs-clipboard` successively in the configured core build. Run
`VMSync` for one-second polling until Ctrl-C; `VMSync ONCE` imports one host
snapshot and exits. Only one daemon is admitted. The device uses `CLIPS:` or
its standard `RAM:Clipboards` fallback.

The first poll imports the Mac clipboard. Later polls compare the host revision
and guest write ID; our own writes update these baselines and are not echoed.
A changed Mac clipboard wins a conflict already present at poll start. A new
guest copy detected during a host download defers the poll. Host publication is
conditional on the last observed revision. Failed sessions leave baselines
unchanged and are retried, so disconnecting does not reset the initial policy.
Complete bounded IFF data is staged before writing locally; the stock device
cannot roll back a disk write failure. Guest ID checking and clipboard writing
are not an atomic compare-and-swap. A stock deferred CBD_POST read can wait for
its producer inside BeginIO, and that device's AbortIO cannot cancel the wait;
this occurs before acquiring the transport session.

For the desktop, package with `--profile wanderer --virtio-gpu --usb-desktop
--host-services --desktop-session forever`, plus explicit build/EFI/output
paths. The package includes both commands/devices and starts VMSync from the
startup sequence. It expands SYS to the existing 16 MiB FAT layout to fit the
USB and host-service components. Launch with `--gpu --usb-input --host-services`.
The expansion and startup contents are verified during packaging.

Add `--clipboard-probe --desktop-session 20` for the desktop qualification
package. Run it with `tools/run-vz.py --desktop-startup --usb-input
--require-usb-desktop --require-desktop-session 20 --require-desktop-clipboard`,
plus disk/output paths. The runner selects an isolated named Mac pasteboard.
A separate guest process reads the exact initial seed through clipboard.device,
holds the foreground transport session across two polling periods, writes text,
and checks that the daemon publishes it after release. It also executes the
independently loaded VMExchange HELLO, imports and exports an image, then checks
unchanged host/local revisions over two more polling periods. This process
never invokes the sync engine itself. All startup, HID, tick and probe markers
must pass. Normal session contention/reopen is covered. A separate existing-Editor test
passes text Paste/Edit/Save/Select All/Copy through host VZ-view events, with
independent WORK-file and private Mac pasteboard byte checks and clean shutdown.
It waits for desktop startup before launching Editor and for its active document
before input. Editor requires its pinned `workbench/tools/Edit/catalogs` Git
submodule; Editor is now included in the normal application payload. The exact test
artifacts and earlier failed preparations are recorded in
[qualification status](STATUS.md). Physical shortcuts,
GUI image operations, forced transport failures and prolonged reliability
are not qualified by this text-editor test.
