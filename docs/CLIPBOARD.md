# VM clipboard bridge

The host service transports the native AROS clipboard's IFF payload. Host
conversion is separate from the stream transport and guest clipboard device;
the protocol can be carried by another transport for physical machines.

Host support is implemented and tested on a private named NSPasteboard. The
VZ launcher supplies the general pasteboard to HostServices when launched with
`--host-services`. No guest synchronization daemon or desktop auto-start is
implemented yet: the host endpoint alone does not provide automatic copy/paste.

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
main queue. The guest device still supports one open stream; the future daemon
and file command must serialize sessions and avoid echoing their own updates.

## Verification

`make test-clipboard-codec` checks Unicode/Latin-1, bounded IFF parsing, ILBM
planes/masks/padding/ByteRun1, PNG conversion, named-pasteboard publication and
stale/malformed-write refusal. `make test-clipboard-services` exercises actual
socket framing with a multi-frame Unicode clipboard, image publication,
revision conflicts, size rejection and abandoned uploads. Both use fresh named
pasteboards and leave the user's general clipboard untouched.

These are host tests, not guest clipboard-device or end-to-end VM qualification.
The latter, automatic synchronization, reconnect behavior, and desktop copy/paste
remain required before claiming the clipboard part of stage 6 complete.
