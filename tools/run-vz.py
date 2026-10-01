#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Qualify actual headless AROS boot under Virtualization.framework."""
from vz_contract import expected_markers, markers_match
import argparse
import hashlib
import json
import re
import runpy
from pathlib import Path
import shutil
import subprocess
import time
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--require-desktop-session',type=int,metavar='SECONDS')
p.add_argument('--require-host-services',action='store_true')
p.add_argument('--require-desktop-clipboard',action='store_true')
p.add_argument('--require-clipboard-sync',action='store_true')
p.add_argument('--require-file-commands',action='store_true')
p.add_argument('--require-vsock-device',action='store_true')
p.add_argument('--require-vsock',action='store_true')
p.add_argument('--require-network-config',action='store_true')
p.add_argument('--require-network-packets',action='store_true')
p.add_argument('--require-network-irq',action='store_true')
p.add_argument('--require-sana-device',action='store_true')
p.add_argument('--require-desktop-network',action='store_true')
p.add_argument('--require-desktop-work',choices=['write','restart'])
p.add_argument('--require-network-dns',action='store_true')
p.add_argument('--require-network-stack',action='store_true')
p.add_argument('--require-network-echo',action='store_true')
p.add_argument('--require-network-tcp',action='store_true')
p.add_argument('--no-input-events',action='store_true',help='negative control: withhold host input events')
p.add_argument('--require-input',action='store_true')
p.add_argument('--require-usb-desktop',action='store_true')
p.add_argument('--usb-input',action='store_true')
p.add_argument('--require-xhci-driver',action='store_true')
p.add_argument('--require-xhci-enumeration',action='store_true')
p.add_argument('--desktop-startup',action='store_true')
p.add_argument('--require-gpu-scanout',action='store_true')
p.add_argument('--require-gpu',action='store_true')
p.add_argument('--require-gpu-irq',action='store_true')
p.add_argument('--filesystem-phase',choices=['write','restart'])
p.add_argument('--storage-phase',choices=['write','restart'])
p.add_argument('--require-pci-hidd',action='store_true')
p.add_argument('--require-virtio-device',action='store_true')
p.add_argument('--require-msi-frame',action='store_true')
p.add_argument('--require-msi-irq',action='store_true')
p.add_argument('--disk',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--timeout',type=float,default=45)
a=p.parse_args()
if a.require_desktop_clipboard and not (a.desktop_startup and a.require_desktop_session):
    p.error('--require-desktop-clipboard requires a desktop session')
if a.require_clipboard_sync: a.require_file_commands=True
if a.require_file_commands:
    a.require_host_services=True
    if not a.filesystem_phase: p.error('--require-file-commands requires --filesystem-phase')
if a.require_host_services and (not a.require_pci_hidd or a.require_vsock or a.require_vsock_device): p.error('--require-host-services requires PCI and excludes the echo probes')
if a.require_vsock_device: a.require_vsock=True
if a.require_vsock and not a.require_pci_hidd: p.error('--require-vsock requires --require-pci-hidd')
if a.require_desktop_work and not (a.desktop_startup and a.require_desktop_session):
    p.error('--require-desktop-work requires a desktop session')
if a.require_network_tcp:
    if a.desktop_startup: a.require_desktop_network=True
    else: a.require_network_stack=True
if a.require_desktop_network:
    if not (a.desktop_startup and a.require_desktop_session): p.error('--require-desktop-network requires a desktop session')
    a.require_network_dns=True
if a.require_network_dns and not a.require_desktop_network: a.require_network_stack=True
if a.require_network_echo: a.require_network_stack=True
if a.require_network_stack:
    a.require_sana_device=True
if a.require_sana_device:
    a.require_network_irq=True
if a.require_network_irq:
    a.require_network_packets=True;a.require_msi_frame=True;a.require_msi_irq=True
if a.require_network_packets: a.require_network_config=True
if a.require_network_config and not a.require_pci_hidd: p.error('--require-network-config requires --require-pci-hidd')
if a.no_input_events and not a.require_input: p.error('--no-input-events requires --require-input')
if a.require_input and not (a.require_usb_desktop and a.require_desktop_session):
    p.error('--require-input requires --require-usb-desktop and --require-desktop-session')
if a.require_desktop_session is not None and (not a.desktop_startup or not 1 <= a.require_desktop_session <= 3600):
    p.error('--require-desktop-session requires --desktop-startup and 1..3600 seconds')
if a.require_usb_desktop and not (a.desktop_startup and a.usb_input):
    p.error('--require-usb-desktop requires --desktop-startup --usb-input')
if a.require_xhci_enumeration and not a.require_xhci_driver:
    p.error('--require-xhci-enumeration requires --require-xhci-driver')
if a.require_xhci_driver and not (a.usb_input and a.require_pci_hidd):
    p.error('--require-xhci-driver requires --usb-input and --require-pci-hidd')
if a.usb_input and not (a.desktop_startup or a.require_gpu):
    p.error('--usb-input requires a graphics profile')
if a.desktop_startup and (a.require_gpu or a.require_pci_hidd):
    p.error('--desktop-startup uses the resident desktop, not the PCI/GPU probe profile')
if a.require_gpu_irq and not a.desktop_startup: p.error('--require-gpu-irq requires resident desktop')
if a.require_gpu_scanout and not a.require_gpu: p.error('--require-gpu-scanout requires --require-gpu')
if a.require_gpu and not a.require_pci_hidd: p.error('--require-gpu requires --require-pci-hidd')
if a.filesystem_phase and not a.require_virtio_device: p.error('--filesystem-phase requires --require-virtio-device')
if a.storage_phase and not a.require_virtio_device: p.error('--storage-phase requires --require-virtio-device')
if a.require_virtio_device and not a.require_msi_irq: p.error('--require-virtio-device requires --require-msi-irq')
if a.require_msi_irq and not a.require_msi_frame: p.error('--require-msi-irq requires --require-msi-frame')
if a.require_msi_frame and not a.require_pci_hidd: p.error('--require-msi-frame requires --require-pci-hidd')
if a.output.exists(): p.error('output must not exist')
a.output.mkdir(parents=True);a.output=a.output.resolve()
disk=a.output/'system.img';shutil.copyfile(a.disk,disk)
log=a.output/'console.log'
marker_counts=expected_markers(a)
input_requested=False
pixels_verified=False
capture_count=0
passed=False;reason='timeout';start=time.monotonic()
with log.open('w') as out:
    proc=subprocess.Popen([str(root/'build/macaros-vm'),str(disk),str(a.output/'state'),'--headless','--force-stop']+(['--gpu-window'] if a.require_gpu_scanout or a.require_input else ['--gpu'] if a.require_gpu or a.desktop_startup else [])+(['--usb-input'] if a.usb_input else [])+(['--input-probe'] if a.require_input else [])+(['--network'] if a.require_network_config or a.require_desktop_network else [])+(['--vsock-probe'] if a.require_vsock else [])+(['--host-services'] if a.require_host_services or a.require_desktop_clipboard or a.require_desktop_work else [])+(['--clipboard-test'] if a.require_clipboard_sync or a.require_desktop_clipboard or a.require_desktop_work else []),stdout=out,stderr=subprocess.STDOUT)
    try:
        while time.monotonic()-start<a.timeout:
            text=log.read_text(errors='replace')
            if a.require_input and not a.no_input_events and not input_requested and '[VM] input observer ready' in text:
                (a.output/'state/input.request').write_text('window-keyboard-pointer-v1\n')
                input_requested=True
            if 'FAIL:' in text: reason='guest-failure';break
            if a.require_host_services and '[B2Q] AROS startup contract PASS' in text and '[VM] host file put get containment PASS' not in text:
                reason='host-services-not-run';break
            if a.require_vsock and '[B2Q] AROS startup contract PASS' in text and ('[VM] vmbridge read write abort reopen PASS' if a.require_vsock_device else '[VM] vsock two 64KiB streams PASS') not in text:
                reason='vsock-not-run';break
            if a.require_network_packets and '[B2Q] AROS startup contract PASS' in text and '[VM] virtio network DHCP exchange PASS' not in text:
                reason='network-packets-not-run';break
            if a.require_network_irq and '[B2Q] AROS startup contract PASS' in text and '[VM] virtio network interrupt delivery PASS' not in text:
                reason='network-irq-not-run';break
            if a.require_sana_device and '[B2Q] AROS startup contract PASS' in text and '[VM] SANA query configure abort reopen PASS' not in text:
                reason='sana-device-not-run';break
            if a.require_network_stack and '[B2Q] AROS startup contract PASS' in text and '[VM] AROSTCP DHCP address PASS' not in text:
                reason='network-stack-not-run';break
            if a.require_network_echo and '[B2Q] AROS startup contract PASS' in text and '[VM] AROSTCP host UDP exchange PASS' not in text:
                reason='network-echo-not-run';break
            if a.require_usb_desktop and '[KI] desktop stable' in text and '[VM] desktop two HID bindings PASS' not in text:
                reason='desktop-without-hid-bindings';break
            if a.require_desktop_session is not None and '[B2Q] AROS startup contract PASS' in text and '[KI] hold complete' not in text:
                reason='desktop-session-not-run';break
            if a.require_gpu_scanout and not pixels_verified and '[VM] virtio GPU scanout ready' in text and capture_count<10:
                window=re.search(r'window-id=(\d+)',text)
                if window:
                    capture_count+=1
                    shot=a.output/f'scanout-{capture_count}.png'
                    captured=subprocess.run(['/usr/sbin/screencapture','-x','-o','-l',window[1],str(shot)],capture_output=True,timeout=5)
                    (a.output/'capture.log').write_bytes(captured.stdout+captured.stderr)
                    if captured.returncode==0 and shot.exists():
                        decoder=runpy.run_path(str(root/'tests/check-aros-render-frame.py'))
                        w,h,rows=decoder['decode'](shot)
                        actual=[decoder['pixel'](rows,int(w*x),int(h*y)) for x,y in [(0.25,0.25),(0.75,0.25),(0.25,0.75),(0.75,0.75)]]
                        expected=[(255,0,0),(0,255,0),(0,0,255),(255,255,255)]
                        pixels_verified=all(abs(a-b)<16 for pixel,color in zip(actual,expected) for a,b in zip(pixel,color))
                        (a.output/'pixels.json').write_text(json.dumps({'actual':actual,'expected':expected,'passed':pixels_verified})+'\n')
            session_ok=(a.require_desktop_session is None or (time.monotonic()-start>=a.require_desktop_session and text.count('[KI] tick')>=max(1,a.require_desktop_session//2)))
            network_ok=not a.require_network_config
            if a.require_network_config:
                host_mac=re.findall(r'network-config attachment=NAT mac=([0-9a-f:]{17})',text)
                guest_mac=re.findall(r'\[VM\] virtio network MAC=0x([0-9a-f]{16})',text)
                network_ok=len(host_mac)==len(guest_mac)==1 and int(host_mac[0].replace(':',''),16)==int(guest_mac[0],16)
            bridge_irqs=re.findall(r'\[VMBridge\] closed IRQ completions=(\d+)',text)
            bridge_irq_ok=not a.require_vsock_device or (len(bridge_irqs)==2 and all(int(n)>0 for n in bridge_irqs))
            services_ok=not a.require_host_services
            if a.require_desktop_clipboard:
                services_ok=text.count('clipboard-test private named pasteboard seeded')==1 and len(bridge_irqs)>=10 and all(int(n)>0 for n in bridge_irqs)
            if a.require_host_services and '[VM] host file put get containment PASS' in text:
                shared=a.output/'state/Shared';fixture=shared/'vmbridge-qualification.bin'
                expected=bytes((i*37+i//251)&255 for i in range(65536))
                names=['vmbridge-qualification.bin'];expected_irqs=2
                command_file_ok=True
                if a.require_file_commands:
                    names.insert(0,'command-upload.bin');expected_irqs=5
                    command_file=shared/'command-upload.bin'
                    command_file_ok=command_file.is_file() and command_file.read_bytes()==bytes(((i*37+11)^(i>>8))&255 for i in range(65536))
                if a.require_clipboard_sync:
                    expected_irqs=19
                    command_file_ok=command_file_ok and text.count('clipboard-test private named pasteboard seeded')==1
                services_ok=fixture.is_file() and fixture.read_bytes()==expected and sorted(p.name for p in shared.iterdir())==names and len(bridge_irqs)==expected_irqs and all(int(n)>0 for n in bridge_irqs) and command_file_ok
            if services_ok and bridge_irq_ok and network_ok and session_ok and (not a.require_gpu_scanout or pixels_verified) and markers_match(text,marker_counts): passed=True;reason='headless-startup-contract';break
            if proc.poll() is not None: reason='early-exit';break
            time.sleep(.1)
    except Exception as error:
        reason='runner-error: '+str(error)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill();proc.wait();passed=False;reason='stop-timeout'
if a.require_gpu_scanout and not pixels_verified and reason=='timeout' and markers_match(text,marker_counts):
    reason='gpu-pixels-unverified'
boot=subprocess.run(['mtype','-i',str(disk),'::BOOT.LOG'],capture_output=True)
(a.output/'firmware.log').write_bytes(boot.stdout)
verdict={'result':'PASS' if passed else 'FAIL','reason':reason,
         'sana_device_verified':passed and a.require_sana_device,
         'vsock_stream_verified':passed and a.require_vsock,
         'vsock_device_verified':passed and a.require_vsock_device,
         'vsock_device_irq_verified':passed and (a.require_vsock_device or a.require_host_services),
         'desktop_clipboard_daemon_verified':passed and a.require_desktop_clipboard,
         'clipboard_sync_engine_verified':passed and a.require_clipboard_sync,
         'host_file_exchange_verified':passed and a.require_host_services,
         'file_commands_verified':passed and a.require_file_commands,
         'desktop_work_phase':a.require_desktop_work,
         'desktop_work_verified':passed and bool(a.require_desktop_work),
         'desktop_network_autostart_verified':passed and a.require_desktop_network,
         'network_dns_verified':passed and a.require_network_dns,
         'network_host_udp_verified':passed and a.require_network_echo,
         'network_tcp_verified':passed and a.require_network_tcp,
         'gpu_irq_verified':passed and a.require_gpu_irq,
         'network_stack_address_verified':passed and a.require_network_stack,
         'network_config_verified':passed and a.require_network_config,'network_packets_verified':passed and a.require_network_packets,'network_irq_verified':passed and a.require_network_irq,'input_events_withheld':a.no_input_events,'desktop_session_seconds':a.require_desktop_session,'desktop_session_verified':passed and a.require_desktop_session is not None,
         'backend':'Virtualization.framework','host_shutdown_mode':'force-stop','graphics':a.require_gpu or a.desktop_startup,'usb_input_exposed':a.usb_input,'xhci_mapping_required':a.usb_input and a.require_pci_hidd,'input_verified':passed and a.require_input,'usb_hid_bindings_verified':passed and a.require_usb_desktop,'xhci_driver_required':a.require_xhci_driver,'xhci_enumeration_required':a.require_xhci_enumeration,'usb_child_descriptors_verified':passed and a.require_xhci_enumeration,'usb_transfers_verified':False,'desktop_startup':a.desktop_startup,'desktop_pixels_verified':False,'gpu_query_required':a.require_gpu,'gpu_scanout_required':a.require_gpu_scanout,'pixels_verified':pixels_verified,'storage_phase':a.storage_phase,'filesystem_phase':a.filesystem_phase,
         'pci_hidd_required':a.require_pci_hidd,'msi_frame_required':a.require_msi_frame,'msi_irq_required':a.require_msi_irq,'virtio_device_required':a.require_virtio_device,
         'seconds':round(time.monotonic()-start,2),
         'input_disk_sha256':hashlib.sha256(a.disk.read_bytes()).hexdigest(),
         'launcher_sha256':hashlib.sha256((root/'build/macaros-vm').read_bytes()).hexdigest()}
(a.output/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict));raise SystemExit(0 if passed else 1)
