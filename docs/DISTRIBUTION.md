# AROS distribution for Macaros VM

`make distrib` builds the standard AROS desktop runtime and packages an autonomous
Macaros VM application. It follows the runtime boundary of hosted Macaros's
`graft/make-aros-release.sh` (file revision
`f44b2ce96738f2af4522310f7b5e44b54fd9f1ee`): C, Classes, Devs, Extras, Fonts, L, Libs, Locale,
Prefs, S, Storage, System, Tools, Utilities and WBStartup, with desktop icons.
Zed, Ferail and Moonstone are excluded, as are the Developer SDK, debug symbols,
crash files and native Apple diagnostic commands. Runtime Devs payloads use
an explicit portable/VM allowlist; Apple hardware modules are excluded from
that directory. Hosted Macaros is reference
material only: its executables and macOS drivers are never copied into the VM.

The build uses the VM-configured AArch64 SDK with ordinary x18 and pinned AROS
translation submodules. Explicit runtime targets live in
`distribution/targets.json`; the build fails on errors or unknown targets.
This runtime profile includes classic image/text datatypes, not every optional
AROS port (e.g. Mesa/Gallium, HEIC/AV1 or third-party applications).

```sh
make distrib
```

Inputs can be selected explicitly:

```sh
make distrib VM_DISTRIB_BUILD=/absolute/configured-vm-build \
  VM_DISTRIB_EFI=/absolute/BOOTAA64.EFI \
  VM_DISTRIB_OUTPUT=/absolute/new-output-directory
```

The existing VM build and EFI loader must have been prepared as documented in
BUILD.md. The output must be new; packaging refuses to replace user disks or an
existing distribution. Source changes must be committed before recording the
signed application. The result is `artifacts/distribution/Macaros VM.app`.
Signing is local/ad-hoc, not Developer ID notarization or public publication.

## Disk and startup

The fixed disk layout stays 64 MiB boot plus 256 MiB WORK. A small RAM system
starts storage, mounts WORK and waits for its publication. It starts the VM shutdown and input services from RAM, then assigns SYS
to `VMWORK0:System` (the `WORK:System` directory), updates the normal AROS
assigns, loads datatypes/preferences and starts the desktop and network stack.
System files and preferences are persistent, and the full distribution does not
have to fit in the 16 MiB bootstrap RAM filesystem. Applications and documents
can use the rest of WORK. Startup stages are recorded in
`WORK:distribution-startup.log`.

The distribution app uses the separate `Distribution` profile under
`~/Library/Application Support/Macaros VM/`. Opening it does not replace the
existing minimal `Default` profile. An explicit `MACAROS_VM_STATE_DIRECTORY`
still overrides this. A profile is seeded only on its first launch. The old
boot-only image updater is not a distribution updater: it preserves WORK and
therefore cannot install or upgrade `WORK:System`.

Each staged runtime file has its size and SHA-256 recorded in the embedded
provenance. Packaging rejects symlinks, wrong-architecture ELF files and Mach-O
host binaries, and checks every installed runtime file by reading it back from
the generated WORK filesystem. Boot modules and VM mount/network settings come
from the regular package-core path. AROS licence files accompany the runtime.

Shutdown inhibits the whole WORK volume, including SYS. If the acknowledgement
is lost or Virtualization.framework refuses to stop, the desktop may remain
visible but SYS is unavailable. Do not resume work in that state: terminate the
VM process and relaunch it. A lost acknowledgement does not prove a successful
flush. The boot-only updater rejects distribution disks: their kernel and
WORK:System must be upgraded together.

The VM boot payload reserves 128 MiB of guest RAM for relocated boot modules,
page tables and the executable allocation arena. The arena receives up to
64 MiB (bounded by the remaining reservation); this is separate from the disk
layout and the 16 MiB RAM filesystem. A full desktop must have room to load
codecs and applications alongside USB, networking and host services. Physical
boot retains its existing arena limit.
