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

New profiles start with the French PC105 AZERTY keymap. The boot sequence runs
`LoadKeymap RESTORE` from `ENVARC:Keymap`; `LoadKeymap pc105_f` applies it to
an already running VM and saves the choice for later boots. Users can choose
another installed keymap with the same command. An existing profile keeps its
own saved preference when a new app is packaged.

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

To upgrade an existing distribution profile, quit its VM and run the offline
upgrade with the old and new applications (both are required for their package
inventories):

```sh
python3 tools/upgrade-distribution.py \
  --old-app '/path/to/old/Macaros VM.app' \
  --new-app '/path/to/new/Macaros VM.app' \
  --disk "$HOME/Library/Application Support/Macaros VM/Distribution/system.img" \
  --output "$HOME/Library/Application Support/Macaros VM/Distribution/upgraded.img"
```

The output is a new 320 MiB disk with the new app's boot volume and
`WORK:System`, plus the previous profile's personal files and directories.
Changed `System/Prefs/Env-Archive` values remain active. Other changed system
files that collide with the new package are saved under `WORK:Upgrade-Saved/System`
for manual review (or a numbered `Upgrade-Saved-N` directory if that name is
already used); the new package version takes effect. The tool reports those
paths in `saved_conflicts`, verifies copied bytes and never overwrites the old
disk or an existing output. It refuses a running profile through the VM's state
lock. After verifying the new disk, move the old `system.img` aside and rename
`upgraded.img` to `system.img` while the VM is stopped. Keep the old app and disk
until the new profile boots to your satisfaction.

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

`make test-page-arena` checks the real allocator’s range boundaries under host
address/undefined-behaviour sanitizers for both native and VM arena limits.
Set `AROS_CORE_SOURCE` if the core worktree is not the sibling `aros-core-vm`.
