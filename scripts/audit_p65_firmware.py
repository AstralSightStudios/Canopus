#!/usr/bin/env python3
"""Read-only exact-P65 input audit; never approves a device deployment.

Accept the complete OTA or its AP payload. Report external trampoline targets
separately from AP bytes; their presence does not establish a callable ABI.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

TARGET = 'xiaomi-p65-3.100.043'
AP_SHA = 'a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858'
RESOURCE_SHA = '17d0667f3ab5af38979ac29a555c3c30baaa83276d64d77263252f3cd75ff96a'
BASE = 0x0C0C0000
MAX_INPUT = 512 * 1024 * 1024
TRAMPOLINES = {
    'object_set_size': 0x0C8BDEC8,
    'event_add': 0x0C8BCE78,
    'event_remove': 0x0C8BD6D8,
    'list_create': 0x0C8BC758,
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def audit(path):
    path = Path(path)
    if not path.is_file() or path.stat().st_size > MAX_INPUT:
        raise ValueError('Expected a regular input no larger than 512 MiB')
    resource = None
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            if not 1 <= len(entries) <= 256:
                raise ValueError('Invalid OTA entry count')
            names = [i.filename for i in entries]
            if len(set(names)) != len(names):
                raise ValueError('Duplicate OTA entry')
            ap_entries = [i for i in entries if i.filename == 'vela_ap.bin']
            if len(ap_entries) != 1:
                raise ValueError('Expected one root vela_ap.bin')
            for entry in ap_entries + [i for i in entries if i.filename == 'vela_resource.bin']:
                if (entry.is_dir() or entry.flag_bits & 1 or
                        entry.external_attr >> 16 & 0o170000 == 0o120000 or
                        entry.file_size > MAX_INPUT or
                        entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)):
                    raise ValueError('Invalid AP/resource entry')
            if ap_entries[0].file_size != 11793540:
                raise ValueError('Unexpected AP payload size')
            ap = archive.read(ap_entries[0])
            if 'vela_resource.bin' in names:
                resource = archive.read('vela_resource.bin')
                if digest(resource) != RESOURCE_SHA:
                    raise ValueError('P65 resource SHA-256 mismatch')
    else:
        if path.stat().st_size != 11793540:
            raise ValueError('Unexpected raw AP size')
        ap = path.read_bytes()
    if digest(ap) != AP_SHA:
        raise ValueError('P65 AP SHA-256 mismatch')
    veneers = []
    for name, address in TRAMPOLINES.items():
        offset = address - BASE
        # Exact Thumb ldr.w pc,[pc,#-0] trampoline and its destination.
        if ap[offset:offset + 4] != bytes.fromhex('5ff800f0'):
            raise ValueError(f'Trampoline instruction mismatch: {name}')
        destination = struct.unpack_from('<I', ap, offset + 4)[0]
        veneers.append({'name': name, 'apAddress': hex(address),
                       'destination': hex(destination),
                       'destinationInApMapping': BASE <= (destination & ~1) < BASE + len(ap),
                       'status': 'static-only; implementation and ABI not verified'})
    return {
        'schemaVersion': 1, 'target': TARGET, 'device': 'REDMI Watch 6 (P65)',
        'firmwareVersion': '3.100.043', 'apSha256': AP_SHA,
        'resourceSha256': digest(resource) if resource is not None else None,
        'resourceBytes': len(resource) if resource is not None else None,
        'quickappFilesRootEvidence': {
            'literal': '/data/quickapp/files/com.tencent.wechatrtos/',
            'apAddress': '0xc39091c',
        },
        'graphicsTrampolines': veneers,
        'lvglFilesystemDriverEvidence': {
            'initializer': '0xc7358f4', 'driver': '0x3c4501d0',
            'driverBytes': 52, 'letter': '/', 'initialCacheBytes': 0,
            'openSlot': '0x3c4501dc', 'openCallback': '0xc7357d5',
            'writeSlotOffset': 20, 'readSlotOffset': 24,
            'callbackPrependsSlash': True, 'successHandle': 'fd+1',
            'failureHandle': 0,
            'status': 'exact AP static recovery; native initializer/open tested offline; no live hook',
        },
        'lvglPathCacheEvidence': {
            'nativeWrapper': '0xc73bca5',
            'decodedCacheGlobal': '0x3c45012c',
            'headerCacheGlobal': '0x3c450130',
            'cacheDrop': '0xc73baa9',
            'keyBytes': 24,
            'headerKeySourceOffset': 0, 'headerKeyTypeOffset': 4,
            'decodedKeySourceOffset': 4, 'decodedKeyTypeOffset': 8,
            'fileSourceType': 1, 'dropUserData': None,
            'status': 'AP wrapper/source classifier tested offline; downstream retirement modeled; no live UI integration',
        },
        'deploymentEnabled': False,
        'blockers': [
            'P65 deployment withdrawn after unresolved Logo boot-loop report',
            'Corona P65 UI owner/theme transaction/refresh incomplete; path cache API tested offline only',
            'External resident code destination is not proven by AP/host tests',
            'No exact-firmware device lifecycle/recovery validation',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.firmware)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f'error: {error}\n')
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
