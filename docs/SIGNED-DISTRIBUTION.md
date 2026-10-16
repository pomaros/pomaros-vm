# Signed preview distributions

`make` and the ordinary app packager remain ad-hoc development builds. The
distribution recipe copies an existing qualified application into a new staging
directory, preserves its actual guest provenance, and signs the copy with
Developer ID, hardened runtime and a secure timestamp. VM uses only Apple's
virtualization entitlement. The QEMU launcher needs no virtualization/JIT
entitlements: the separately installed QEMU executable owns those privileges.

Signing and Apple timestamps are intentionally not byte deterministic. The
immutable guest seed and exact signed launcher hashes are recorded and checked.
Never replace the original package, personal profile or guest source identity.

```sh
python3 tools/distribute.py prepare --app '/path/PomAROS VM.app' --output artifacts/signed-vm --public-preview --release-tag v0.1.1-preview.1
python3 tools/check-vz-distribution.py --app 'artifacts/signed-vm/PomAROS VM.app' --output artifacts/signed-vm-qa
python3 tools/distribute.py zip --distribution artifacts/signed-vm --output artifacts/PomAROS-VM-app.zip --acceptance artifacts/signed-vm-qa/verdict.json
python3 tools/distribute.py submit --file artifacts/PomAROS-VM-app.zip --profile YOUR_PROFILE --evidence artifacts/vm-app-notary
```

Reserve the project's VM resource before the bounded windowed acceptance check.
For QEMU, use that repository's wrapper and `tools/qualify.py`, passing the
staging directory's `guest-source.lock` with `--core-lock` when retaining an older
qualified guest payload. The exact signed executable must pass before archiving.
The QEMU distribution remains a development build requiring installed QEMU 11;
its coordinated guest flush/quit transport is not implemented.

Submission returns immediately and records its ID and submitted SHA-256. Check
it with `info --id ID --profile YOUR_PROFILE --evidence DIR`. After Apple reports
`Accepted`, staple the **application first**, then create and submit the DMG:

```sh
python3 tools/distribute.py staple --file 'artifacts/signed-vm/PomAROS VM.app' --evidence artifacts/vm-app-ticket
python3 tools/distribute.py dmg --distribution artifacts/signed-vm --output artifacts/PomAROS-VM-0.1.1-preview.1-arm64.dmg --acceptance artifacts/signed-vm-qa/verdict.json
python3 tools/distribute.py submit --file artifacts/PomAROS-VM-0.1.1-preview.1-arm64.dmg --profile YOUR_PROFILE --evidence artifacts/vm-dmg-notary
# Once the DMG submission is Accepted:
python3 tools/distribute.py staple --file artifacts/PomAROS-VM-0.1.1-preview.1-arm64.dmg --evidence artifacts/vm-dmg-ticket
```

The final DMG must contain the already stapled app. Mount it read-only, verify
both tickets, signatures and the contained executable's accepted hash, then
record the final DMG checksum after stapling. The tools retain command logs,
signatures, entitlements, runtime identity, submission IDs and ticket checks.

Public previews use `prepare --public-preview --release-tag v0.1.1-preview.1`.
That mode requires the current public core lock and a matching app version;
it refuses historical-source overrides. Product and core snapshots, exact
submodules, external component sources and notices must accompany the same
GitHub release. Review the actual payload/source inventory before upload.
Original FreeType/JPEG/other source-cache notices, explicit FreeType/IJG credits,
Unicode V3 and LLVM compiler-rt licence texts are carried in the app and guest
documentation. The staging manifest hashes those notice files.
This tool signs and submits to Apple; the release coordinator publishes the
reviewed source assets and binaries separately.

ScreenCaptureKit is an optional host diagnostic. Its result and error are
recorded separately from the required signed desktop, HID/GPU, normal exit,
guest quiesce and immutable-seed checks. A capture failure never supplies an
image or weakens those mandatory acceptance conditions.

For a retained historical payload, `prepare --guest-source-lock PATH` requires
a lock containing its actual source URL, exact revision, branch and ABI. It
refuses to replace the current public pin with an old private commit silently.
Existing signed/notarized archives and their provenance are never modified by
source acquisition or pin updates.
