#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Bounded local UDP peer for the guest socket probe; never a general echo service."""
import argparse
import ipaddress
import json
from pathlib import Path
import secrets
import socket
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bind', required=True, help='explicit host IPv4 address reachable from VZ')
p.add_argument('--output', required=True, type=Path)
p.add_argument('--timeout', type=float, default=180)
a = p.parse_args()
address = ipaddress.IPv4Address(a.bind)
if address.is_unspecified or address.is_multicast or not 0 < a.timeout <= 600:
    p.error('specific unicast address and timeout in (0,600] required')
a.output.mkdir(parents=True, exist_ok=False)
challenge = secrets.token_bytes(32)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
    peer.bind((str(address), 0))
    port = peer.getsockname()[1]
    (a.output/'NETECHO.REQ').write_bytes(address.packed + port.to_bytes(2, 'big') + challenge)
    print(f'ready {address}:{port}', flush=True)
    deadline = time.monotonic() + a.timeout
    result = {'result': 'FAIL', 'reason': 'timeout', 'endpoint': [str(address), port]}
    while time.monotonic() < deadline:
        peer.settimeout(max(0.001, deadline-time.monotonic()))
        try:
            data, sender = peer.recvfrom(2048)
        except socket.timeout:
            break
        # Ignore unrelated datagrams, cap response to the exact 32-byte challenge.
        if data != challenge:
            continue
        peer.sendto(data, sender)
        result.update(result='PASS', reason='challenge-received-and-replied', guest=list(sender))
        break
(a.output/'verdict.json').write_text(json.dumps(result, indent=2)+'\n')
raise SystemExit(0 if result['result'] == 'PASS' else 1)
