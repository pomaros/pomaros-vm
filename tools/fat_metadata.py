# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Canonical metadata for newly generated FAT16 images, never mounted disks."""
import datetime
import struct

EPOCH = int(datetime.datetime(2026, 8, 29, tzinfo=datetime.timezone.utc).timestamp())
FAT_DATE = ((2026-1980)<<9)|(8<<5)|29


def normalize_fat16(source):
    data = bytearray(source)
    if len(data)<512 or data[510:512]!=b'\x55\xaa':
        raise ValueError('missing FAT boot signature')
    sector,spc,reserved,fats,entries,total,media,spf=struct.unpack_from('<HBHBHHBH',data,11)
    total=total or struct.unpack_from('<I',data,32)[0]
    if (sector!=512 or not spc or spc&(spc-1) or not reserved or
            fats not in (1,2) or not entries or not spf or total*sector!=len(data)):
        raise ValueError('invalid FAT16 geometry')
    root_start=(reserved+fats*spf)*sector
    root_bytes=((entries*32+sector-1)//sector)*sector
    data_start=root_start+root_bytes;cluster_bytes=spc*sector
    clusters=(len(data)-data_start)//cluster_bytes
    if not 4085<=clusters<65525 or (clusters+2)*2>spf*sector:
        raise ValueError('expected bounded FAT16 geometry')
    fat=reserved*sector;seen=set();pending=[(root_start,root_bytes)]

    def directory(cluster):
        while cluster<0xfff8:
            if cluster<2 or cluster>=clusters+2 or cluster in seen:
                raise ValueError('invalid directory chain')
            seen.add(cluster)
            pending.append((data_start+(cluster-2)*cluster_bytes,cluster_bytes))
            cluster=struct.unpack_from('<H',data,fat+2*cluster)[0]

    while pending:
        start,length=pending.pop()
        for at in range(start,start+length,32):
            if data[at]==0:break
            if data[at]==0xe5 or data[at+11]==0x0f:continue
            if data[at+11]&0x08:
                # A FAT label has no LFN chain; some mtools versions emit one.
                previous=at-32
                while previous>=start and data[previous+11]==0x0f:
                    data[previous]=0xe5;previous-=32
            data[at+13]=0
            struct.pack_into('<HHH',data,at+14,0,FAT_DATE,FAT_DATE)
            struct.pack_into('<HH',data,at+22,0,FAT_DATE)
            if data[at+11]&0x10 and data[at]!=ord('.'):
                directory(struct.unpack_from('<H',data,at+26)[0])
    return data
