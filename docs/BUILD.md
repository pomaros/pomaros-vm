# Building the guest

The host builds with `make`; the UEFI loader builds with
`make -C boot/arosboot`. `tools/build-core.sh` builds the changed bootstrap and
kernel in an isolated, configured AROS build. It deliberately selects explicit
metatargets to reuse the existing cross toolchain.

The supported configuration builds into a new directory with
`tools/configure-core.py`, followed by the payload target and
`tools/package-core.py` as shown below. The packager consumes only products
from that build and refuses missing components. No native object directory or
seed bundle is needed. Core source lives in `../aros-core-pomaros`, on the
private `pomaros-common` branch; qualified revisions are recorded in
`STATUS.md`.

Guest C and assembly target AArch64 ELF without `-ffixed-x18`. EFI targets
AArch64 PE/COFF with the firmware calling convention; host code follows the
macOS ABI. The diagnostic `tools/prepare-esp.py --base` helper is for replacing
individual artifacts during diagnostic experiments, not a complete rebuild.
Use separate, newly named ESP directories and disk images for each run.

The MIT early-console protocol header is authored in `include/vm-console.h`;
its imported copy in the APL core is `include/aros/apple/vmconsole.h`. Keep
them identical when updating the protocol. The boot allocation schema adds
`AROS_BOOT_ALLOCATION_EARLY_CONSOLE` for a single retained 4-KiB record. Pin the matching core with each image.

## Portable platform probes

```sh
python3 tools/qualify-platform.py --output artifacts/platform-NEW
```

The default firmware corpus is `../aros-raspi/payloads/pi4/bcm2711-rpi-4-b.dtb`;
use `--rpi4-dtb` to select another copy. The runner copies it and selects the
modeled PL011 console in that copy. It records the original hash and override.
The same guest binary runs on QEMU virt/HVF and raspi4b/TCG, with the FDT captured
from guest RAM and compared with dtc/fdtget. Timer and register-restoration
negative controls retain independent QMP register evidence on failure.

These probes do not boot the whole AROS system or access a physical Pi.
The VZ startup and HVF Wanderer checks remain separate acceptance runs.

Host parser corpus (ASan/UBSan):

```sh
python3 tests/early-fdt.py --tree PATH.dtb --output artifacts/fdt-corpus.json
```

`make test` includes the synthetic bus/alias/interrupt fixture. Each corpus
input is checked at every truncated length and with deterministic mutations.
The core and EFI builders share one libfdt import. A configured guest build
must regenerate the platform makefile after build-rule changes, or mirror its
new shared-FDT include paths and objects in the isolated generated makefile.

## Kernel deadline timer qualification

`tools/run-qemu.py --require-timer --gic-version 3 --esp ESP --output NEW`
requires three real `timer.device` requests to complete and at least three
kernel timer firings, in addition to startup and the Wanderer pixel check.
Repeat with `--gic-version 2`. Failed runs stop the CPU and save QMP registers
before terminating the process. The optional `--dtb` input is copied into the
run artifact directory.

For the negative control, build an isolated negative kernel with the sole
change in `vm_irq.c` from `AROSGICEnable(&controller, timer_id, trigger)` to
`AROSGICEnable(&controller, timer_id ^ 2U, trigger)`. Package that kernel into
a separate ESP, then restore and rebuild the positive source. Verify its hash
matches the original positive kernel. Run:

```sh
python3 tests/kernel-timer-control.py --esp POSITIVE_ESP \
  --negative-esp NEGATIVE_ESP --output artifacts/kernel-timer-NEW
```

The negative must reach GIC initialization, time out before the wait proof,
and preserve CPU registers. Changing the timer IRQ in QEMU's firmware FDT is
not an equivalent negative: EFI consumes that description too and can block
before AROS starts.

The kernel generated makefile must include `vm_irq` and the conditional shared
GIC/libfdt objects and compilation rules from its source template. As with the
bootstrap rules, refresh it after pulling build changes. VZ startup qualification requires GIC readiness and the three-wait timer
proof. Use the matching EFI loader and kernel; the generated FDT includes
named timer interrupts.

The shared configuration reserves x18 only for the `darwin-aarch64` hosted
target. VM guests and native Apple builds use x18 normally; future Raspberry Pi
builds follow that same policy. The VM configure helper validates the generated
flags and refuses a build that reserves x18. It does not rewrite generated
configuration or reuse another target's object files.

Stage two passes four scalar GIC window bounds in protected boot tags. The
kernel requires all four, with no duplicates, and matches both FDT register
windows exactly before GIC MMIO access. Old stage-two binaries without these
tags fail the VM IRQ contract; package matching stage two and kernel binaries.
`make test` checks tag emission, capacity, incomplete records, duplicates and
overlap under ASan/UBSan.

## Kernel device-handler chain qualification

VZ requires `[VM] IRQ handler chain PASS`. With QEMU, add
`--require-irq-chain --require-timer`, and exercise both GIC versions. The proof
uses a software-pended SPI and the public handler APIs, including rejection of
unconfigured/reserved sources, unsupported polarity, cyclic attribute lists,
and reconfiguration while handlers are installed. It validates callback data
and final-removal hardware masking; it is not a real peripheral or SMP test.
The kernel generated makefile must include the `modifyirq` architecture override.

For a dispatch negative control, replace only the call to `krnRunIRQHandlers`
in the isolated VM adapter with a no-op, build and package a separate ESP.
The guest must pass its timer waits and then print `[VM] FAIL: IRQ handler chain`;
the runner must reject the boot. Restore the source, rebuild and compare the
positive kernel hash before using the build again.

## Fresh configuration

```sh
python3 tools/configure-core.py --output "../Build/pomaros-vm/clean-core"
make -C "../Build/pomaros-vm/clean-core" -j8 apple-boot-payloads
make -C boot/arosboot BUILD=../../build/uefi-clean
python3 tools/package-core.py --build "../Build/pomaros-vm/clean-core" \
  --efi build/uefi-clean/BOOTAA64.EFI --output build/fresh-headless
python3 tools/make-disk.py --esp build/fresh-headless --output build/fresh-headless.img
python3 tools/run-vz.py --disk build/fresh-headless.img --output artifacts/fresh-vz
python3 tools/run-qemu.py --esp build/fresh-headless --profile core \
  --require-timer --require-irq-chain --require-platform --output artifacts/fresh-hvf
```

The output directory must not exist. The helper uses the prebuilt cross
toolchain in `../aros-crosstools` (override with `--toolchain`), records the
configure command and log, and selects `--enable-vm-guest`. This profile exports
`AROS_VM_GUEST=1` to generated makefiles and validates ordinary x18 use in C
and C++ flags. The profile selects guest platform behavior; x18 reservation
belongs only to the `darwin-aarch64` hosted ABI. Native Apple builds also use
ordinary x18. Each target builds its own objects from the shared source. It initializes the pinned catalog submodules required by DOS, base commands
(LoadResource/R), ASL, muimaster and Wanderer before configuring.

The packager consumes ordered paths in `boot/headless-modules.json` or
`boot/wanderer-modules.json`,
builds the ABI command with the fresh SDK, creates the deterministic SYS image,
and records source revision and input hashes. It refuses missing products and
a dirty core source; there is no seed-bundle fallback. `--profile core` on the
QEMU runner checks startup and requested IRQ/timer proofs without claiming a
desktop. The default `wanderer` profile retains the desktop pixel gate.
Repeating packaging can verify byte-identical bundles; that is separate from
comparing two independent complete builds. Build and package the desktop with:

```sh
make -C "../Build/pomaros-vm/clean-core" -j8 apple-wanderer-payloads
make -C "../Build/pomaros-vm/clean-core" -j8 workbench-c
python3 tools/package-core.py --build "../Build/pomaros-vm/clean-core" \
  --efi build/uefi-clean/BOOTAA64.EFI --profile wanderer --output build/fresh-wanderer
python3 tools/run-qemu.py --esp build/fresh-wanderer --output artifacts/fresh-desktop \
  --require-timer --require-irq-chain --gic-version 3
```

The desktop package includes the standard Assign and MakeDir commands, creates
RAM:T and assigns T: before services start. Desktop SYS uses the expanded
16 MiB FAT layout; a 1.44 MiB seed is a packaging intermediate.

This uses QEMU's framebuffer. It does not qualify the VZ virtio GPU or input
devices. The desktop's source and binary identities are in package provenance;
qualification results are recorded in the handoff log.

## Supervising the early virtio console

Run the VZ firmware bridge and the same guest console transport under QEMU/HVF:

```sh
python3 tools/run-qemu.py --esp ESP --profile core --console virtio \
  --require-timer --require-irq-chain --output artifacts/virtio-console-NEW
```

This selects ACPI, GICv3 and a modern PCI virtio console. Guest output is in
`serial.log`; PL011 firmware output is retained separately in
`firmware-serial.log`. On failure the runner stops the guest and captures CPU
registers through QMP, independently of virtio. A deliberately short timeout
checks this capture path but is not a guest fault reproduction. The ordinary
framebuffer lane retains `--console pl011` and firmware FDT discovery.

The headless package includes `openfirmware.resource` and `lddemon.resource`.
Its ABI command also checks the existing OpenFirmware APIs and reads PCI vendor
IDs through a supervisor callback. ECAM remains inaccessible from EL0. VZ always
requires this platform proof; use `--require-platform` for the QEMU core profile.
The Wanderer package retains its separate CLI probe and resident profile.

With the fresh SDK available, run `make test-openfirmware` for the FDT-to-resource
constructor under ASan/UBSan, including failure at every allocation. This target
is separate from the SDK-independent host test suite (`make test`).

The generic PCI ECAM class can be built independently with
`make -C "../Build/pomaros-vm/clean-core" -j8 kernel-hidd-pciecam`.
`make test-pci-ecam-class` uses the actual class methods and SDK structures under
host sanitizers; it does not qualify class loading or device enumeration in a
guest. The VM platform must supply admitted mappings and a configuration-write
completion barrier before registering this class with the PCI subsystem.

To qualify real shared-library loading and OOP configuration methods, add
`--pci-hidd` when packaging the headless profile. This installs PCIPROBE and
five libraries in SYS, all recorded in provenance. Run VZ or QEMU with
`--require-pci-hidd`; otherwise a normal headless startup does not assert this
extra proof. The command disposes its driver instance before unloading, while
libraries remain resident. It additionally registers and removes the flat root bridge through the PCI
subsystem, comparing its enumeration against direct ECAM discovery. The probe
grants EL0 access to the first ECAM bus temporarily and restores supervisor
permissions after removal. After the temporary test, it loads `vmpci.hidd`, which owns a permanent root
bridge and its callback. The kernel unloads the installer command and runs a
fresh command to verify enumeration and configuration writes through the
retained bridge. The permanent module keeps its admitted mappings for the VM
lifetime and reserves the boot console. DMA translation is limited to buffers
allocated through its explicit platform policy. On a discovered GICv2m frame,
the resident driver provides one MSI-X table entry per device.
Build it with `make -C "../Build/pomaros-vm/clean-core" -j8 kernel-hidd-vmpci`;
this target is limited to the VM profile. Both resident markers are required
by `--require-pci-hidd`. The default package without that option does not load
the platform driver.

The PCI qualification also maps every visible virtio common configuration
through the driver's MapPCI method and reads its queue count. It temporarily
enables PCI memory decode and restores the original command word. The required
PCI gate includes this register-access proof; the separate queue checks below
qualify DMA transfers.

`make test-virtio-pci-map` exercises the mapping engine used by the existing
virtio transport, including translated CPU addresses, every partial mapping
failure and notification-window overflow. It is also part of `make test`.
The guest PCI probe uses this same engine for real register reads. This does
not qualify virtio.device queue activation, DMA or controller teardown.

`make test-vmpci-dma` tests the VM allocation/translation implementation under
host sanitizers with a non-identity physical-address model, out-of-range and
freed addresses, and noncontiguous pages. `make test-pci-ecam-class` also tests
callback forwarding and rejection of an incomplete DMA policy. These use the
fresh SDK and are separate from `make test`.

The PCI guest qualification submits one virtio entropy descriptor, polls for
completion with an architectural-counter deadline, then waits for reset
acknowledgement before freeing the DMA buffer. QEMU's PCI profile adds an
entropy device to match VZ. The mandatory DMA completion marker proves this
bounded queue transfer; it does not prove peripheral IRQs or block I/O.

`make test-virtio-queue` runs the actual block queue implementation under host
sanitizers with simulated non-identity DMA. The PCI guest probe also compiles
`virtio_hw.c` and `virtio_queue.c` unchanged and reads sector zero of its isolated
boot disk, checking completion length, the AROS reply and the boot signature.
The required block-read marker is separate from entropy completion. This is a
polled shared-driver-code test, not loading the complete virtio.device or
qualifying writes, filesystem persistence or interrupts.

For a PCI-associated GICv2m frame, the probe reads MSI_TYPER through its admitted
supervisor mapping and validates the SPI range (or its FDT override). Add
`--require-msi-frame` together with `--require-pci-hidd` to either VM runner to
require that proof. An absent/unsupported controller cannot pass this gate.
This flag verifies discovery and register access. Add `--require-msi-irq` to
require a real block completion interrupt through the resident platform route.
The block probe then consumes the used ring only after its IRQ handler runs,
and resets the device before detaching the handler and freeing DMA. QEMU's
GICv3 topology may require an ITS implementation instead of a v2m frame.

`make test-vmpci-irq` compiles the actual resident routing implementation with
mock PCI/kernel endpoints under fatal sanitizers. It checks ownership, bounded
capability/BAR validation, failure unwinding, table/control restoration and
per-device source ownership across 512 reopen cycles. MSI-X AddInterrupt requires
memory decode already enabled and unmasked unrelated table entries are refused.
RemoveInterrupt requires a quiescent device. Each SPI stays reserved for one PCI
device until reboot; the same device reuses it after reset, without retaining the
old callback or table pointer. The frame bounds distinct device reservations,
not session count. Late posted messages are never redirected to a different
device; handlers must validate current queue state. Published device objects are
kept alive by the resident bridge. Hot removal requires a separate lifetime/drain
design.

To exercise the installed driver rather than only shared queue code, package
with `--pci-hidd --virtio-device`. This also installs bus.hidd, storage.hidd
and virtio.device. On VZ require all four gates:

```sh
python3 tools/run-vz.py --disk DISK.img --output artifacts/device-NEW \
  --require-pci-hidd --require-msi-frame --require-msi-irq \
  --require-virtio-device --timeout 20
```

The added test opens unit zero through Exec, queries geometry and sends CMD_READ
through the driver's worker/IRQ path. It checks exact completion length and the
boot-sector signature. A timed-out request retains its storage until the bounded
runner shuts down the VM. The device and published storage classes remain
resident. This read gate does not qualify writes, flush durability or filesystems.

`make test-virtio-io` compiles the real BeginIO path with mocked submission under
fatal ASan/UBSan. It checks whole-transfer bounds, logical-block alignment,
64-bit offsets, submission rejection with one list removal, and immediate
request destruction by a completion recipient. An accepted asynchronous request
must not be touched after publication, including its IOF_QUICK flag.

For isolated block persistence, first create the ordinary 64 MiB image from a
`--pci-hidd --virtio-device` package, then run:

```sh
python3 tools/run-storage-vz.py --disk DISK.img --output artifacts/storage-NEW
```

The harness verifies the FAT volume ends at 64 MiB, copies it and appends a
4 MiB test area. Exact geometry and a full signature block opt the guest into
writing one 4 KiB pattern beyond the filesystem. The driver must have negotiated
FLUSH; CMD_UPDATE must complete successfully. The first VM writes, flushes and
reads back; after its process stops, the host checks the pattern and all guard
bytes. A second fresh VM reads the resulting image and must report the restart
proof, followed by another host byte check. Each VM has its own state directory.
The input image is preserved. This proves block persistence across VM restart,
not filesystem recovery or survival of host power loss.

For a mounted FAT filesystem over the installed virtio driver:

```sh
python3 tools/run-filesystem-vz.py --disk DISK.img --output artifacts/filesystem-NEW
```

This preserves the 64 MiB input and appends a separately formatted 16 MiB FAT
volume containing an opt-in seed. The guest registers `VMTEST0:` using the
resident FAT handler and a bounded DosEnvec; its volume name is `VMTEST:`.
It verifies the seed, creates a directory, writes 64 KiB, closes and renames
the file, and checks every byte. Inhibit uses the device name and DOSTRUE to
flush/detach the volume; a negotiated device FLUSH follows. A fresh second VM
must read the existing file. Host mtools independently checks contents, seed
and disappearance of the pre-rename name after each VM. Corruption is an error,
not a request to recreate the file. This gate does not inject interruption.

For an abrupt VM-process cut during open-file writes:

```sh
python3 tools/run-crash-vz.py --disk DISK.img --output artifacts/crash-NEW
```

The harness first requires the clean filesystem/restart gate. It copies that
result and appends an unused 4 MiB trailer, selecting crash mode by exact 84 MiB
geometry. The mounted volume remains bounded to 64..80 MiB. After verifying the
durable reference file, the guest opens UNFINISHED.BIN and repeatedly writes
64 KiB without close, flush or inhibit. The host requires the workload marker
and terminates VZ with SIGKILL, verifying return code -9. It preserves the exact
post-kill image, removes only the unused trailer from a recovery copy, and runs
read-only fsck_msdos on the extracted volume. A fresh guest must remount and
verify the reference; host mtools verifies it before and after recovery.

PASS requires the observed workload, forced termination, clean read-only FAT
check and intact durable file. It does not require a complete interrupted file,
pinpoint an in-flight DMA write, or establish host-power-loss durability.

The filesystem guest also saves `CONSOLE.BIN`, a read-only snapshot of the
retained VMConsole record obtained in supervisor mode, before flushing the
volume. Extract it independently of console output with mtools:

```sh
mcopy -i artifacts/RUN/write/system.img@@67108864 ::CONSOLE.BIN /tmp/console.bin
```

Interpret the record with the exact `include/vm-console.h` and package provenance
from that run; it is a diagnostic structure, not a versioned external format.
A log timeout with intact files proves continued guest work, not a CPU freeze.
`make test-console-timeout` tests the shared TX implementation with an injected
clock: measured deadline, retained buffer ownership after timeout, delayed
completion and subsequent writes, 16-bit wrap, and invalid-descriptor rejection.
The TX deadline is one architectural-counter second per drain attempt. A timeout
cannot authorize descriptor reuse or permanently poison an otherwise valid
queue. Initialization/control-queue deadlines are separate from this TX test.

For the first VZ GPU transport gate, add `--require-gpu` to `tools/run-vz.py`
with `--require-pci-hidd`. The launcher accepts `--gpu` alongside `--headless`
to expose a 1024x768 virtio GPU without creating a window or USB input devices.
The PCI probe uses the existing driver's MapPCI/reset/feature negotiation and
owned DMA allocation, submits GET_DISPLAY_INFO on controlq, and validates the
complete response and enabled mode. Reset must be acknowledged before DMA is
freed. This is a polled control command, not rendered pixels, GPU IRQs, a HIDD
or a desktop. The wire layout follows
[Virtio 1.2 §5.7](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).

The GPU probe also exercises fenced RESOURCE_CREATE_2D, ATTACH_BACKING,
TRANSFER_TO_HOST_2D, SET_SCANOUT and RESOURCE_FLUSH using a 1024x768 owned DMA
buffer. It holds a four-quadrant pattern for five seconds, then disables the
scanout, detaches/unrefs and resets before freeing memory. Every response must
match its fence and expected size/type. To require independently rendered pixels:

```sh
python3 tools/run-qemu.py --esp ESP --profile core --console virtio \
  --require-pci-hidd --require-gpu-scanout --output artifacts/gpu-HVF-NEW
```

The QMP capture selects the GPU device directly. On VZ,
`--require-gpu --require-gpu-scanout` opens a dedicated test window and captures
only that window with screencapture. Both lanes decode the PNG using the existing
render verifier and check quadrant colors as well as guest cleanup/boot proofs.
Failure to capture or verify pixels cannot pass the visual gate. A successful
control-command sequence alone does not qualify VZ rendering.


## Virtio desktop profile

Build `kernel-hidd-virtiogfx` in the configured VM core. The module reuses the
framebuffer chunky bitmap/display classes, replacing pixel presentation with
virtio GPU transfer/flush. It uses deferred boot-mode display setup, has separate OOP class IDs and retains its PCI/DMA
context for the display lifetime. The initial mode is 1024x768 truecolor;
commands wait for registered PCI queue interrupts with a one-second timer.device
bound, retaining reset quarantine on failure. Queue parsing remains in task
context; the callback only acknowledges and signals the serialized caller.
This implementation uses the existing single-CPU IRQ lifetime contract.
The VM desktop readiness observer samples at timer ticks so sleeping render
commands cannot make a tight observation loop expire prematurely.
Use `run-vz.py --require-gpu-irq --desktop-startup` with the desktop run flags
to require actual interrupt completion. Each backend needs its own runtime
and pixel qualification. This profile includes
OpenFirmware and PCI/ECAM/vmpci residents and replaces the EFI graphics resident.
The kernel verifies their exact order, priorities and flags.

```sh
python3 tools/package-core.py --build BUILD --efi EFI --profile wanderer \
  --virtio-gpu --output ESP
python3 tools/run-qemu.py --esp ESP --profile wanderer --console virtio \
  --virtio-desktop --output artifacts/desktop-NEW
```

The desktop gate requires driver registration, the complete guest startup
contract, and the existing Wanderer render check on a QMP capture of the virtio
GPU itself. Guest registration alone cannot pass it. VZ visual/input acceptance
is tracked separately in STAGE6.md.

To exercise the same resident desktop under VZ without claiming captured pixels:

```sh
python3 tools/make-disk.py --esp ESP --output DISK.img
python3 tools/run-vz.py --disk DISK.img --desktop-startup --output artifacts/vz-desktop-NEW
```

This gate requires the guest startup/foreground proof and both PCI and graphics
registration markers. Its verdict explicitly leaves desktop pixels unverified.
The normal launcher can open that disk in a VZ window; input drivers are a
separate pending milestone.


## VZ USB input discovery

`--usb-input` exposes the same USB keyboard and absolute pointing device in
headless runs that the ordinary VZ window configuration exposes. Combine it
with a graphics profile. In the PCI probe profile it additionally requires an
xHCI capability-mapping marker; this tests register admission, not USB transfers
or delivered input events. The verdict keeps `input_verified=false`.

```sh
python3 tools/run-vz.py --disk PCI_PROBE.img --require-pci-hidd \
  --require-msi-frame --require-msi-irq --require-virtio-device \
  --require-gpu --usb-input --output artifacts/usb-discovery-NEW
```

The probe records vendor/product/class, walks the bounded PCI capability chain,
maps BAR0 through the owning PCI driver and reads xHCI capability registers.
It temporarily enables memory decoding and restores the command word. It does
not reset/run the controller or publish DMA buffers. Desktop startup can also
be checked with `--desktop-startup --usb-input`, independently of input delivery.

### Full xHCI driver startup and enumeration probes

Package `--pci-hidd --xhci-driver` to include `pcixhci.device`, Poseidon and
an explicit probe request. Run with `--require-pci-hidd --require-gpu
--usb-input --require-xhci-driver`. This gate opens the actual driver and
checks its query interface; it does not prove USB transfer or HID delivery.
The probe retains the unit and heap-backed request until the bounded VM is
terminated, so this gate does not exercise controller teardown.

For Poseidon child-device discovery, build `kernel-usb-classes-hub` and
`kernel-usb-classes-hubss`, add `--xhci-enumerate` when packaging, and add
`--require-xhci-enumeration` to the runner. That mode requires hardware attach,
root enumeration and at least two connected child devices with device
descriptors. It does not qualify keyboard/pointer input or cancellation.
Both modes use isolated image copies and preserve `input_verified=false`.

### Poseidon HID binding in the VZ desktop

Build `kernel-usb-classes-hid` in addition to the hub classes. Package with
`--profile wanderer --virtio-gpu --usb-desktop`. The SYS image contains the
Poseidon library, xHCI device, hub/hubss/HID classes and VMUSB command.
Its Startup-Sequence runs VMUSB before G4Ready and EndCLI. Driver loading runs
in the normal DOS process; the boot-resident profile is unchanged.

Run with `--desktop-startup --usb-input --require-usb-desktop`. This requires
both normal desktop startup and two connected, configured child devices with
interfaces bound to the loaded HID class. `usb_hid_bindings_verified` records
that result; `input_verified` remains false. Binding alone does not demonstrate
that host key/pointer events reached a guest window, or prolonged guest use.

For startup barrier regression fixtures, `package-core.py --startup-sequence PATH`
accepts an explicit 1–512 byte script with the Wanderer profile, without
`--usb-desktop`. The package records the script hash in its input manifest.

### Desktop session policy

Build the EFI loader with `make -C boot/arosboot`. For a live desktop, package
with `--profile wanderer --virtio-gpu --usb-desktop --desktop-session forever`.
Use that loader with `--efi build/uefi/BOOTAA64.EFI`. Launch the resulting disk
with `build/pomaros-vm DISK_IMAGE STATE_DIRECTORY` to show its VZ window.
The process owns the session until it is stopped. Keyboard/pointer event
acceptance requires separate qualification; HID binding does not prove it.

For automated runs, use `--desktop-session 30` at packaging and
`--desktop-startup --usb-input --require-usb-desktop --require-desktop-session 30
--timeout 45` with `tools/run-vz.py`. The runner requires shell launch, bounded
hold completion, at least one tick per two requested seconds and the normal
startup contract; it also checks elapsed host time. A session not run, or a
ready desktop without the required HID marker, fails explicitly.

The package writes `AROS/SESSION.CFG` with ASCII decimal seconds (1–3600) or
`forever` (diagnostic UI) or `desktop` (production UI with the monitor widget), followed by one
newline, and records its hash. The VZ ACPI bridge
translates it into existing `aros,desktop-hold-seconds` or
`aros,desktop-hold-forever` chosen strings. Production `desktop` also sets
`aros,desktop-hide-shell=1` to omit the diagnostic Shell while retaining the monitor widget. An absent file selects the boot
proof with no hold. Invalid contents reject the platform setup. This is VZ
loader policy; a firmware-provided FDT uses its own chosen properties.

### Host-to-guest window input qualification

Add `--input-probe` when packaging the USB desktop with a bounded session of
at least 30 seconds. Startup launches VMInput asynchronously; it opens an
active Intuition window and observes IDCMP messages. Run with
`--desktop-startup --usb-input --require-usb-desktop --require-desktop-session 30
--require-input --timeout 45`.

The runner shows a VZ window and waits for the guest observer's ready marker
before requesting host events. The launcher delivers two pointer movements,
a left-button press/release and an A-key press/release to its own
[VZVirtualMachineView](https://developer.apple.com/documentation/virtualization/vzvirtualmachineview).
No global event posting or guest input injection is used. Guest success requires
the corresponding raw-key/button codes and both expected pointer positions
within two pixels. With `WA_TabletMessages` enabled, it also requires the
extended `TABLETA_ButtonBits` attribute to change from pressed to released.
The full session/startup gates must then pass. This qualifies
a synthetic host-event path through USB/HID/Intuition, not physical input.
`--no-input-events` is the negative control: the guest must fail without host
stimulus. Pixel capture and prolonged use are separate gates.

### VZ network configuration

The launcher accepts `--network` to expose a virtio network device with Apple's
[NAT attachment](https://developer.apple.com/documentation/virtualization/vznatnetworkdeviceattachment).
Its random locally administered unicast MAC is persisted in
`STATE_DIRECTORY/mac-address.txt` and validated when reused. An invalid saved
address stops configuration before VM startup.

Package a headless PCI profile with `--pci-hidd`, then use
`tools/run-vz.py --require-pci-hidd --require-network-config ...`. This exposes
the device and compares the guest's generation-checked MAC read through PCI
mapping with the host configuration. The probe restores PCI command and
feature-selector registers and releases mappings. It does not submit network
queues or packets; `network_packets_verified` is false. A guest network driver
and stack remain required for network use.

### Host header-generator regression

`python3 tests/genmodule-short-names.py --output artifacts/UNIQUE_DIRECTORY`
builds the real core genmodule with ASan/UBSan and generates both graphics
headers and a short-name/vararg fixture. `--source` selects an isolated source
revision; `--expect-failure` verifies that an old revision triggers a sanitizer
failure. Logs and a JSON verdict are retained. Leak checking is disabled for
this process-lifetime generator; bounds and undefined behavior checks are fatal.


### Nested command startup

`package-core.py` requires an explicit `--efi` path; old build directories can
contain loaders without the PCI description needed by the current guest.
Add `--command-startup` to a headless package without the vsock device. This
packages VMExchange and checks NewStackSwap, LoadSeg, and RunCommand with an
expected clean failure when HELLO cannot open the absent service. Run QEMU
with `--require-command-startup --profile core --console virtio` and the usual
`--esp` / `--output` arguments. The gate requires the full startup contract,
command main entry and expected command result. On failure the runner preserves
QMP registers, current instructions and stack words before ending the VM.

File services have their own [protocol and qualification](HOST-SERVICES.md).

### Persistent desktop WORK volume

Add `--work-volume` to the Wanderer/virtio-GPU `package-core.py` command.
`make-disk.py` reads this requirement from `provenance.json` and creates a new
320 MiB image: 64 MiB FAT boot filesystem followed by 256 MiB FAT16 labelled
WORK. `make-disk.py --work-volume` also explicitly requests this layout.
Existing outputs are refused; this does not upgrade or resize existing images.

Startup uses stock LoadResource for the non-expunging bus/storage HIDDs, then
Mount with DEVS:WORK.mount. Device VMWORK0: is distinct from volume WORK:.
Startup temporarily locks through VMWORK0: using Assign, then removes that
assign: FAT replies to startup before publishing WORK, so Mount alone is not
a volume-readiness barrier. Geometry: virtio.device unit 0, 512-byte sectors,
one sector per cylinder, low cylinder 131072, high cylinder 655359. Mask retains
Mount's native-width default. This is explicit VM geometry, not GPT discovery.
SYS is RAM-backed; use WORK for persistent files. Reuse the same disk on later
launcher invocations. Test runners copy disks: reuse their resulting system.img.

The combined desktop passes a 64 KiB write, FAT drain, negotiated virtio FLUSH,
and fresh-VM exact read with independent host byte checks. Add --work-probe to
a bounded package for this test; run-vz --require-desktop-work write requires
the creation phase, and --require-desktop-work restart requires existing-file
readback. Reuse the resulting system.img, never regenerate its WORK volume.
With --host-services, normal host quit/SIGTERM waits for the configured VMSync
volume to be inhibited and flushed before stopping VZ. --work-volume packages
VMSync VOLUME=VMWORK0: automatically. Failed/missing acknowledgement keeps the
VM running; --force-stop explicitly bypasses this handshake. run-vz uses that
bypass for boot-contract qualification; run-shutdown-vz.py tests the real
handshake. This flushes filesystem buffers, not unsaved application data, and
only covers the configured WORK device.

### Standalone macOS application

After building the launcher and a normal `--work-volume --network-stack
--usb-desktop --desktop-session desktop` package, commit source changes and run:

```sh
python3 tools/package-app.py --esp artifacts/desktop-safe-quit-bundle --output 'artifacts/PomAROS VM.app'
```

The app includes its boot disk, guest provenance and license notices. It is
ad-hoc signed for private local use, not notarized for public distribution.
Opening it without arguments starts the desktop with network and host services.
First launch copies the seed into
`~/Library/Application Support/PomAROS VM/Default/system.img`. Later launches
reuse that disk and state (including NVRAM, MAC and Shared directory). Updating
or replacing the app does not replace a populated disk. A failed initial copy
is not published as system.img. The application locks its state while running; VZ owns the disk attachment
lease. An extra flock on the disk would conflict with VZ itself.

`POMAROS_VM_STATE_DIRECTORY` overrides the state location for isolated testing.
Bundled mode also accepts flags such as `--headless --gpu --usb-input
--clipboard-test`; no disk/state positional arguments are then needed. The
explicit `DISK_IMAGE STATE_DIRECTORY [flags]` command-line form remains available.
The app has a Quit menu; window close and Quit use the guest shutdown handshake.
First-launch storage behavior is exercised by `make test-vm-storage`.

### Deterministic outer disk metadata

`make-disk.py` creates FAT16 boot and optional WORK volumes with fixed serials,
canonical 2026-08-29 timestamps, and explicitly sorted directory/file insertion.
The shared normalizer is also used by SYS expansion. Host creation order, source
mtime and timezone do not change these new disk bytes. Serials identify the
seed layout, not unique VM instances; runtime MAC/NVRAM remain per-state data.
Only newly created image files are normalized, never a user's populated disk.

`python3 tests/disk-reproducibility.py` compares both supported layouts from
oppositely ordered trees, different mtimes/timezones, and verifies every file
including a UTF-8 name. It also checks refusal to overwrite an existing output.
This packaging comparison does not establish independent guest compilation or
launcher/EFI reproducibility.

Launcher release builds use Swift whole-module optimization. Compare separate
source and output directories after entitlements signing. The Makefile is a
launcher prerequisite so changing build flags triggers recompilation; record
the exact compiler and signing inputs with each comparison.

The EFI makefile fixes the PE timestamp to zero. Compare two fresh copies of
boot/arosboot and include with the same explicit `AROS_CORE_SOURCE` and compiler.
App packaging comparisons use frozen inputs and compare signed bytes and modes.
These checks do not replace the full guest/SDK comparison. Observed results and
exact source identities belong in [STATUS.md](STATUS.md).

### Complete current guest comparison and build epoch

New configurations default SOURCE_DATE_EPOCH to the core commit timestamp.
`--source-date-epoch` (or SOURCE_DATE_EPOCH while configuring) selects an explicit
epoch. The helper validates it, records vm-build-environment.json, and writes
make.opts defaults so ordinary later make invocations use the same date. Explicit
environment/make overrides remain possible; changing epoch requires a fresh build.
Generic command/filesystem version-date recipes use the shared UTC date when an
epoch is supplied; ordinary core builds without one retain local-date behavior.
GNU make 3.81 needs an explicit validated epoch argument to the date helper:
makefile exports alone do not reach its $(shell) expressions.

```sh
python3 tests/build-date.py
python3 tools/rebuild-compare.py --output "../Build/pomaros-vm/repro-current-20261001"
```

The comparison tool freezes a clean core clone (preserving repository identity
and disabling pushes), snapshots the configure helper, and creates two new build
directories. Each compiles the desktop, SDK, virtio/USB drivers, base commands,
AROSTCP/DHCP, clipboard, VMExchange and VMSync. It compares every AROS product/SDK
file and symlink plus stage2, not just packaged modules. Four jobs per build by
default; targets within each build run sequentially to avoid generator races.
Progress/logs and the final strict verdict remain in the output directory. It
never reuses objects from an existing native/VM build. A RUNNING verdict or a
successful configure is not a completed comparison.

## Bounded integrated desktop workload

Package a live WORK desktop with `--profile wanderer --virtio-gpu --usb-desktop
--work-volume --desktop-session forever --soak-probe`, plus the required build,
EFI and output paths. The soak flag includes the network and clipboard observers.
Create a new disk with `tools/make-disk.py`, then run
`tools/run-soak-vz.py --disk DISK --output NEW_RUN`.

The guest performs 60 cycles of 64 KiB write/flush/rename, actual VMExchange
PUT/GET, and independent guest readback, with a one-second pause per cycle.
The host requires the cycle sequence, desktop/GPU IRQ/HID/network/clipboard
markers and timer ticks, then requests normal guest-acknowledged shutdown.
It checks all 60 uploaded files and both final WORK files independently after
stop. Workload timeout is 180 seconds; shutdown gets 20 seconds. The runner
copies the disk and uses isolated host services/private clipboard state.
Memory snapshots are observations, not a leak-free assertion. This is a bounded
integrated workload, not exhaustive crash recovery or prolonged reliability.

## Updating an existing VM system while preserving WORK

The application copies its seed only for a new profile. Replacing the .app does
not replace an existing profile's guest system. To prepare updated boot files,
stop the VM and create a separate disk with the new seed and existing WORK:

```sh
python3 tools/update-system-image.py \
  --seed "NEW.app/Contents/Resources/System.img" \
  --disk "OLD_PROFILE/system.img" --output "NEW_PROFILE/system.img"
POMAROS_VM_STATE_DIRECTORY="NEW_PROFILE" "NEW.app/Contents/MacOS/PomAROSVM"
```

Use absolute profile paths when launching from another directory. The helper
supports the fixed 64 MiB boot + 256 MiB WORK layout. It validates both FAT16
volumes, obtains exclusive locks on read-only inputs, copies the new boot volume
and byte-identical existing WORK, then publishes a new file atomically without
replacement. Both inputs remain untouched. A running VZ disk lease, symlink,
unsupported layout or existing destination is refused. New profile machine/network
identity is generated normally; this command copies disk data, not host Shared
files or firmware state. Keep the old profile as the rollback copy.

The complete comparison driver bootstraps each empty output with the first
ordinary target, then executes the remaining targets in one MetaMake process.
MetaMake retains dependency state and processes those roots sequentially; `-j`
applies inside the build, avoiding concurrent graph generators. The generated
batch makefile invokes the configured `CALL` wrapper, which supplies mandatory
host/target variables. A bare direct MetaMake invocation lacks that environment.
Unknown-target diagnostics fail the comparison even if MetaMake exits zero.
Before hashing the complete SDK, the inventory also requires 28 named commands,
services and drivers to contain AArch64 ELF headers; file count alone is not
coverage of those targets.

### Explicit QEMU MSI controller

`tools/run-qemu.py --msi-controller gicv2m` requests QEMU's GICv2m frame
without changing `--gic-version 3`. Other explicit values are `its` and `off`;
`auto` preserves the machine default. This requires a QEMU version exposing
the `virt` machine's `msi` option (present in the local QEMU 11.0.1 help).
The [QEMU machine implementation](https://github.com/qemu/qemu/blob/v11.0.1/hw/arm/virt.c)
describes the controller selection. No QEMU implementation code is imported.

`--require-gpu-irq --virtio-desktop --console virtio` requires the resident
driver's IRQ completion marker in addition to desktop startup and verified
QMP desktop pixels. Selecting an MSI controller alone proves no interrupt
delivery. The current GICv3/GICv2m HVF trial aborts in QEMU before completion;
see the handoff for the retained failure and diagnostic attempt.

## QEMU packaging backend

`tools/package-core.py` and `tools/distribution.py package` accept
`--backend vz|qemu`; the default is `vz`. Both consume the same configured VM
build. QEMU packages retain WORK storage, USB input and networking without
starting VMSync or requiring a vsock host-service provider. Host clipboard,
file exchange and coordinated host shutdown are unavailable in that backend.
The QEMU product owns its launcher and shutdown interface. A bounded test
harness is separate from its interactive launcher.

`python3 tests/package-backend.py` checks the actual argument admission phase
for both backends and rejects unsupported QEMU host-service requests.

## Shared source pin

`CORE.lock` pins the common source URL, branch, revision and guest register
policy. Fetch that private source into `../aros-core-pomaros` and select its
exact clean revision before configuring. The helper validates this pin before
creating any build directory. Experimental builds require an explicit
`--core-lock FILE` with their actual source identity; product packages must use
the product pin. Packages reject a source that changed after configuration.

`make test-host` includes source/ABI admission controls; a correct pin alone
does not establish guest build or runtime qualification. See [STATUS.md](STATUS.md).

## Windowed core regression and supervisor

`tools/run-qemu.py --windowed --supervise` opens a normal Cocoa window, starts
paused and verifies independent QMP register access before resuming. Failures
preserve stopped registers, current instructions and stack. The guest `core`
profile is a headless ABI probe regardless of the visible host firmware window.

`tools/run-vz.py --windowed` exposes the normal firmware/GPU view even for a
headless core guest. Before stopping a failed run it attempts a host process
sample and window capture; kernel trap registers remain in serial output. VZ
provides no public independent guest-register API. A failed capture is recorded
rather than counted as guest or pixel acceptance.
