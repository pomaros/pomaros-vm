#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""One bounded TCP stream transaction with independently checked guest bytes."""
import argparse
import hashlib
import ipaddress
import json
from pathlib import Path
import secrets
import socket
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--bind',required=True)
p.add_argument('--advertise',required=True,help='guest-visible host NAT bridge IPv4')
p.add_argument('--output',required=True,type=Path)
p.add_argument('--timeout',type=float,default=90)
a=p.parse_args()
bind=ipaddress.IPv4Address(a.bind);address=ipaddress.IPv4Address(a.advertise)
if bind.is_multicast or address.is_multicast or address.is_unspecified or not 0<a.timeout<=600:
    p.error('unicast endpoint and timeout in (0,600] required')
if not bind.is_unspecified and bind!=address:p.error('specific bind must equal advertised address')
a.output.mkdir(parents=True,exist_ok=False)
seed=secrets.token_bytes(32)
payload=bytes(seed[i%32]^((i*37+(i>>8))&255) for i in range(65536))
response=bytes(x^0xa5 for x in payload)
result={'result':'FAIL','reason':'not-started','bytes_each_way':65536,
        'upload_sha256':hashlib.sha256(payload).hexdigest(),
        'download_sha256':hashlib.sha256(response).hexdigest()}
deadline=time.monotonic()+a.timeout

def remaining(sock):
    left=deadline-time.monotonic()
    if left<=0:raise TimeoutError('absolute peer deadline')
    sock.settimeout(left)

def receive(sock,size):
    data=bytearray()
    while len(data)<size:
        remaining(sock)
        chunk=sock.recv(size-len(data))
        if not chunk:raise RuntimeError('premature EOF')
        data.extend(chunk)
    return bytes(data)

try:
    with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as listener:
        listener.bind((str(bind),0));listener.listen(1)
        port=listener.getsockname()[1]
        result.update(endpoint=[str(address),port],bind=str(bind))
        (a.output/'NETTCP.REQ').write_bytes(address.packed+port.to_bytes(2,'big')+seed)
        print(f'ready {address}:{port}',flush=True)
        remaining(listener)
        with listener.accept()[0] as stream:
            result['guest']=list(stream.getpeername())
            if receive(stream,32)!=seed:raise RuntimeError('challenge mismatch')
            received=receive(stream,len(payload))
            if received!=payload:raise RuntimeError('upload mismatch')
            remaining(stream)
            if stream.recv(1):raise RuntimeError('unexpected upload tail')
            # Deliberately use a different send boundary from the guest's 1 KiB
            # buffers. TCP may merge or split these writes freely.
            for offset in range(0,len(response),997):
                remaining(stream);stream.sendall(response[offset:offset+997])
            stream.shutdown(socket.SHUT_WR)
            result.update(result='PASS',reason='verified-upload-and-transformed-response-sent')
except (OSError,RuntimeError) as error:
    result['reason']=str(error)
finally:
    (a.output/'verdict.json').write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(result['result']!='PASS')
