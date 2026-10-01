# VM network

The launcher `--network` option attaches the existing virtio-net driver to a
Virtualization.framework NAT interface. Its MAC is persisted in the VM state
directory. The guest uses the resident SANA-II driver and AROSTCP; addresses
come from DHCP. The Mac uses its current network connection, including Wi-Fi.

The headless qualification covers network configuration, raw DHCP traffic,
interrupt delivery, SANA-II lifecycle, an actual AROSTCP DHCP lease and a UDP
socket round trip to a host peer. The peer receives and returns a per-run
32-byte random challenge. The connected guest socket verifies source and bytes;
no public echo service is required. This is not DNS, TCP, Internet-access or
sustained-traffic qualification. Desktop automatic startup and DNS probes are implemented but not qualified:
the combined desktop currently fails its foreground acceptance before DNS runs.
The working headless DHCP/UDP evidence does not qualify this desktop path.

## Host peer and guest gate

Start `tools/network-echo-server.py --bind HOST_IPV4 --output NEW_PEER_DIR
--timeout 90`, where HOST_IPV4 is the host NAT bridge address reachable from
the guest. Package with `tools/package-core.py --pci-hidd
--network-echo-request NEW_PEER_DIR/NETECHO.REQ` and explicit build/EFI/output
paths, then create a disk with `tools/make-disk.py`. Run
`tools/run-vz.py --require-pci-hidd --require-network-echo` with that disk and a
fresh output directory. Both the guest and peer verdicts must pass.

The NAT bridge may not exist until the VM starts. For that case the bounded
peer accepts `--bind 0.0.0.0 --advertise HOST_NAT_IPV4`: bind an ephemeral port
before VM startup, but place the explicit guest-visible bridge address in the
request. Only the exact random challenge is answered; unrelated packets are
ignored and the socket closes after the first valid response or deadline.
The measured bridge was 192.168.64.1; this is an observation, not a fixed
platform or guest configuration. Determine the actual host bridge address for
the environment in use. A specific-address bind must match its advertised
address.

The successful bridge-address test does not explain earlier failures when the
peer used the host Wi-Fi address. Do not infer a driver fault or a general NAT
restriction from those failures. Exact runs and identities are recorded in
[`when john is back.md`](../when%20john%20is%20back.md).

## Desktop integration candidate

`tools/package-core.py --profile wanderer --virtio-gpu --usb-desktop
--host-services --network-stack --desktop-session forever` includes the
resident SANA driver and starts AROSTCP from Startup-Sequence. Network commands,
dependencies and configuration use the expanded 16 MiB SYS image. Logs go to
`RAM:AROSTCP.log`; command output goes to `RAM:AROSTCP-startup.log`.
Launch this candidate with `--gpu --usb-input --host-services --network` so
the requested virtual devices are present.
`CONSOLENAME` is not enabled in this AROSTCP build and must not be configured.

For bounded qualification, add `--network-probe --desktop-session 20` when
packaging, and use `tools/run-vz.py --desktop-startup --usb-input
--require-usb-desktop --require-desktop-session 20 --require-desktop-network`
with explicit disk/output paths. The observer does not start the stack itself;
it waits for bsdsocket.library, DHCP and `gethostbyname("example.com")` using
the DHCP-provided nameservers. No fixed resolver or hosts entry is installed.
The headless variant is `--network-dns --pci-hidd` with runner
`--require-network-dns --require-pci-hidd`.

The desktop candidate remains unqualified. Its foreground failure is preserved
rather than bypassed; kernel diagnostics capture bounded window titles on that
failure path. A later passing DNS marker is required before treating name
resolution as established. A positive DNS result would still not qualify TCP,
HTTP or sustained Internet traffic.
