# Signed internal distributions

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
python3 tools/distribute.py prepare --app '/path/PomAROS VM.app' --output artifacts/signed-vm
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
python3 tools/distribute.py dmg --distribution artifacts/signed-vm --output artifacts/PomAROS-VM-0.1.0-arm64.dmg --acceptance artifacts/signed-vm-qa/verdict.json
python3 tools/distribute.py submit --file artifacts/PomAROS-VM-0.1.0-arm64.dmg --profile YOUR_PROFILE --evidence artifacts/vm-dmg-notary
# Once the DMG submission is Accepted:
python3 tools/distribute.py staple --file artifacts/PomAROS-VM-0.1.0-arm64.dmg --evidence artifacts/vm-dmg-ticket
```

The final DMG must contain the already stapled app. Mount it read-only, verify
both tickets, signatures and the contained executable's accepted hash, then
record the final DMG checksum after stapling. The tools retain command logs,
signatures, entitlements, runtime identity, submission IDs and ticket checks.

These are internal artifacts for John Knipper. Product launcher/tool sources are public in the PomAROS organization. The
corresponding AROS core remains private; appropriate recipient access to that
core and applicable component sources is required before redistribution. No GitHub release upload or public source publication is
part of this recipe. License texts, prerequisites and source-access limits are
included with the distribution and application.
