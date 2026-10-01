# DeviceTree command

`C:DeviceTree` inspects the retained firmware tree through the public
`openfirmware.resource` API. It contains no Apple or VM hardware addresses and
uses no privileged register access. It requires resource version 1, which adds
`OF_GetNodeName` (the borrowed immutable node name, including its unit address).

```text
DeviceTree
DeviceTree DEPTH 1
DeviceTree /chosen PROPERTIES
DeviceTree / PROPERTY compatible
DeviceTree / PROPERTY compatible HEX
DeviceTree PROPERTIES >WORK:device-tree.txt
```

The default view lists absolute node paths and useful properties: model,
compatible strings, device type, status, registers, address ranges, interrupt
properties, cell counts and the selected console. `PATH` selects a subtree;
`DEPTH` limits traversal relative to it (0 means only that node, default 64).
`PROPERTIES` displays every property. `PROPERTY name` reads a single property
of the selected node, without traversing children. `HEX` forces byte output.
AROS Shell's standard `?` argument shows the command template.

Known string properties are displayed as quoted string lists. Other aligned
values are shown as big-endian 32-bit cells, and odd-length values as bytes.
Register cells are **not decoded physical addresses**: their meaning depends on
parent cell counts and bus translations. Control characters are escaped.
Each value is limited to a 256-byte preview with the original size stated;
this is a readable diagnostic, not a lossless DTS/DTB exporter. Traversal is
bounded to 16384 nodes and supports Ctrl+C. Missing nodes/properties return
ERROR (10); absent or older resources return FAIL (20); Ctrl+C returns WARN (5).

This shows firmware descriptions, not driver binding, resource grants, actual
available RAM or live hotplug state. The roadmap's `DEVTREE:` filesystem and
`AddDeviceTree` overlay validation remain separate work. Following its diagnostic
authority boundary, `bootargs` and `.state` values are withheld even when explicitly
selected; authority integration has not been implemented for this command.

Build in the configured core tree with `make workbench-c-devicetree`.
`make distrib` includes it and rejects a distribution missing the binary.
Host regression: `python3 tests/device-tree.py` compiles the actual command with
synthetic DOS/OpenFirmware APIs under ASan/UBSan. It checks string lists,
unaligned big-endian cells, malformed strings, bounded previews, escaped output,
redaction, missing/old resources, selection, depth, Ctrl+C and output failure.
It is complemented by command execution in the actual VM, not a hardware claim.
