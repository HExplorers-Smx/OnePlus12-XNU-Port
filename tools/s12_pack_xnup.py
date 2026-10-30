#!/usr/bin/env python3
"""Create a HakiOS-compatible XNUP payload, local files only; never a boot image.

Header and record format mirror HakiOS's public mkpayload.py. We require a
Mach-O kernel and intentionally refuse huge, duplicate or invalid entries.
"""
import argparse
from pathlib import Path
import struct

MAGIC = 0x50554E58
MAX = 128 * 1024 * 1024

def make(items: dict[str, bytes], cmdline: str = "") -> bytes:
    if cmdline:
        items = {**items, 'cmdline':cmdline.encode('ascii') + b'\0'}
    if 'kernel' not in items:
        raise ValueError('kernel required')
    k=items['kernel']
    if len(k)<32 or k[:8] != struct.pack('<II',0xfeedfacf,0x0100000c):
        raise ValueError('kernel must be arm64 Mach-O')
    body=bytearray()
    for name,data in items.items():
        if not name or len(name.encode('ascii'))>=16 or '\0' in name:
            raise ValueError('bad record name')
        if len(data)>MAX:
            raise ValueError('record too large')
        body.extend(struct.pack('<16sII', name.encode('ascii'),len(data),0))
        body.extend(data)
        body.extend(b'\0'*(-len(data)%16))
    body.extend(bytes(24))
    if len(body)+8>MAX:
        raise ValueError('payload too large')
    return struct.pack('<II',MAGIC,len(body))+body

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--kernel',required=True,type=Path)
    p.add_argument('-o','--output',required=True,type=Path)
    p.add_argument('--cmdline',default='cpus=1')
    a=p.parse_args()
    data=make({'kernel':a.kernel.read_bytes()},a.cmdline)
    a.output.write_bytes(data)
    print(f'{a.output}: {len(data)} bytes, XNUP; NOT a boot image')
