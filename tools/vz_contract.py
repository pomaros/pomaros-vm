# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exact marker multiplicity shared by VZ runs and captured-log controls."""
def expected_markers(a):
    markers=['[VM] OpenFirmware FDT + PCI config PASS',
             '[VM] GIC + CNTV deadline ready',
             '[VM] timer.device three waits PASS',
             '[VM] IRQ handler chain PASS',
             '[KI] dispatch masks: disabled 0x00000000000000c0 plain 0x0000000000000040 PASS',
             '[B2V] runtime vectors + EL0 SVC PASS',
             '[B2D] boot + two-task context dispatch PASS',
             '[B2S] DOS takeover + SYS namespace sentinel PASS',
             '[B2A] portable ABI + emulated TLS + streams + W^X/unload PASS',
             '[B2Q] AROS startup contract PASS']
    if a.desktop_startup:
        markers.remove('[VM] OpenFirmware FDT + PCI config PASS')
        markers += ['[VMPci] resident bridge registered', '[VM] virtio graphics driver registered']
    if a.require_desktop_session is not None:
        markers += ['[KI] shell launched','[KI] hold bounded','[KI] hold complete']
    if getattr(a,'require_desktop_clipboard',False): markers += ['[VM] desktop daemon clipboard seed PASS','[VM] desktop daemon busy session recovery PASS','[VM] desktop clipboard daemon text image no-echo PASS','[VMExchange] hello response=1']
    if getattr(a,'require_clipboard_sync',False): markers += ['[VM] clipboard device text image sync no-echo PASS']
    if a.require_file_commands: markers += ['[VM] VMExchange CLI put get no-replace PASS']
    if a.require_host_services: markers += ['[VM] host file put get containment PASS','[VM] VMExchange command hello PASS']
    if a.require_vsock: markers += [('[VM] vmbridge read write abort reopen PASS' if a.require_vsock_device else '[VM] vsock two 64KiB streams PASS'),'[HOST] vsock 64KiB stream echo PASS']
    if a.require_network_config: markers += ['[VM] virtio network configuration PASS']
    if a.require_network_packets: markers += ['[VM] virtio network DHCP exchange PASS']
    if a.require_network_irq: markers += ['[VM] virtio network interrupt delivery PASS']
    if a.require_sana_device: markers += ['[VM] SANA query configure abort reopen PASS']
    if getattr(a,'require_desktop_network',False): markers += ['[VM] AROSTCP socket library PASS','[VM] AROSTCP DHCP address PASS','[VM] desktop network autostart PASS']
    if getattr(a,'require_network_dns',False): markers += ['[VM] AROSTCP DHCP DNS lookup PASS']
    if a.require_network_stack: markers += ['[VM] AROSTCP socket library PASS','[VM] AROSTCP DHCP address PASS']
    if a.require_network_echo: markers += ['[VM] AROSTCP host UDP exchange PASS']
    if a.require_input: markers += ['[VM] input observer ready','[VM] window keyboard and pointer PASS']
    if a.require_usb_desktop: markers += ['[VM] desktop two HID bindings PASS']
    if a.require_pci_hidd:
        markers += ['[VM] PCI HIDD class + reserved console PASS',
                    '[VM] PCI subsystem enumerate + remove PASS',
                    '[VM] virtio common config via MapPCI PASS',
                    '[VM] virtio reset acknowledged PASS',
                    '[VM] PCI resident install PASS',
                    '[VM] PCI resident bridge after command unload PASS',
                    '[VM] virtio entropy DMA completion PASS',
                    '[VM] virtio block queue sector read PASS']
    if a.usb_input and a.require_pci_hidd: markers += ['[VM] xHCI capability mapping PASS']
    if a.require_xhci_enumeration: markers += ['[VM] Poseidon hardware attach PASS','[VM] Poseidon root enumeration PASS','[VM] xHCI two child descriptors PASS']
    elif a.require_xhci_driver: markers += ['[VM] xHCI driver unit open PASS','[VM] xHCI driver query PASS']
    if a.require_msi_frame: markers += ['[VM] MSI frame mapped read PASS']
    if a.require_msi_irq: markers += ['[VM] virtio block MSI-X completion PASS']
    if a.require_virtio_device: markers += ['[VM] full virtio.device unit open PASS','[VM] full virtio.device IRQ read PASS']
    if a.storage_phase:
        markers += ['[VM] storage FLUSH negotiated PASS']
        markers += (['[VM] storage write flush PASS','[VM] storage write readback PASS'] if a.storage_phase=='write' else ['[VM] storage restart read PASS'])
    if a.filesystem_phase:
        markers += ['[VM] filesystem mount PASS', '[VM] filesystem '+('write flush' if a.filesystem_phase=='write' else 'restart')+' PASS']
    if a.require_gpu: markers += ['[VM] virtio GPU display query PASS']
    if a.require_gpu_scanout: markers += ['[VM] virtio GPU scanout cleanup PASS']
    counts={marker:1 for marker in markers}
    if a.require_vsock: counts['[HOST] vsock 64KiB stream echo PASS']=2
    return counts

def markers_match(text, counts):
    return 'FAIL:' not in text and all(text.count(marker)==count for marker,count in counts.items())
