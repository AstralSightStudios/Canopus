#!/usr/bin/env python3
"""Check UI flag veneers against a stock P65 action's exact Thumb call sites.

This checks the binding direction without modeling the missing ROM bodies.
It does not establish touch routing or module activation on hardware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

SHA = 'a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858'
BASE = 0x0C0C0000
ROOT = Path(__file__).resolve().parents[1]


def firmware_bytes(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            entries = [item for item in archive.infolist()
                       if Path(item.filename).name == 'vela_ap.bin']
            if len(entries) != 1:
                raise ValueError('missing or ambiguous AP image')
            data = archive.read(entries[0])
    else:
        data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('exact P65 3.100.043 AP required')
    return data


def bl_target(data, address):
    first, second = struct.unpack_from('<HH', data, address - BASE)
    if first & 0xF800 != 0xF000 or second & 0xD000 != 0xD000:
        raise ValueError('expected Thumb BL at stock action call site')
    sign = (first >> 10) & 1
    i1 = 1 ^ ((second >> 13) & 1) ^ sign
    i2 = 1 ^ ((second >> 11) & 1) ^ sign
    offset = ((sign << 24) | (i1 << 23) | (i2 << 22) |
              ((first & 0x3FF) << 12) | ((second & 0x7FF) << 1))
    if sign:
        offset -= 1 << 25
    return address + 4 + offset


def verify(data, header):
    cases = (
        ('P65_OBJECT_ADD_FLAG', 0x0C76472C, 2, 0x0C76472E),
        ('P65_OBJECT_CLEAR_FLAG', 0x0C764734, 16, 0x0C764736),
    )
    bindings = {}
    for name, mask_site, mask, call_site in cases:
        if struct.unpack_from('<H', data, mask_site - BASE)[0] != 0x2100 | mask:
            raise ValueError('stock action mask instruction changed')
        target = bl_target(data, call_site)
        match = re.search(r'#define\s+' + name + r'\s+UINT32_C\((0x[0-9a-fA-F]+)\)', header)
        if not match or int(match[1], 16) != target | 1:
            raise ValueError(f'{name} must match stock action veneer {target | 1:#010x}')
        bindings[name] = {'mask': mask, 'veneer': f'{target | 1:#010x}'}
    return {'firmwareSha256': SHA, 'flagBindings': bindings,
            'matchedStockAction': True, 'deviceVerified': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('--header', type=Path,
                        default=ROOT / 'manager/target/p65/canopus_p65_abi.h')
    args = parser.parse_args()
    print(json.dumps(verify(firmware_bytes(args.firmware), args.header.read_text()), indent=2))
