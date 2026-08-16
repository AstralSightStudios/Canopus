#!/usr/bin/env python3
"""Synchronize the complete 3.101.036 symbol inventory into 11-108.

Only names in confirmed_mappings.json receive a 4.100.108 address. Every
other 036 record is copied as an explicit fail-closed FORBIDDEN record with
no guessed address, so inventory parity does not imply ABI confidence.
"""
import copy, json, glob
from pathlib import Path

SRC = Path('targets/xiaomi-band-10-pro-3.101.036/symbols')
ROOT = Path('targets/xiaomi-band-11-4.100.108')
OUT = ROOT / 'symbols'
TARGET = 'xiaomi-band-11-4.100.108'
SHA = '9315ca353f624cec25dfcfc98a95ba959e2d7b24573bf1d6adf16ea10341bd99'

# Existing 11-108 records contain reviewed prototypes/notes for mapped symbols.
existing = {}
for p in OUT.glob('*.json'):
    d = json.loads(p.read_text())
    existing[d['name']] = d
mappings = json.loads((ROOT / 'confirmed_mappings.json').read_text())['by_name']

# Rebuild the directory from the authoritative 036 inventory. This also
# removes stale duplicate-category files left by earlier generators.
for p in OUT.glob('*.json'):
    p.unlink()

for src_path in sorted(SRC.glob('*.json')):
    src = json.loads(src_path.read_text())
    name = src['name']
    d = copy.deepcopy(existing.get(name, src))
    d['symbol_id'] = f"{TARGET}.{d.get('symbol_id','').split('.', 3)[-2] if False else src['symbol_id'].split('.', 3)[2]}.{name}"
    # Preserve the source category from the 036 symbol id.
    parts = src['symbol_id'].split('.')
    category = parts[3] if len(parts) > 3 else 'synchronized'
    d['symbol_id'] = f'{TARGET}.{category}.{name}'
    d['target_id'] = TARGET
    d['provenance'] = {
        'firmware_sha256': SHA,
        'source': ('Exact 4.100.108 IDB semantic confirmation recorded in '
                   'confirmed_mappings.json' if name in mappings else
                   'Synchronized from the complete 3.101.036 inventory; '
                   'no 4.100.108 address has been semantically confirmed.'),
    }
    # Remove old target-specific evidence and promotion claims.
    if name in mappings:
        d['entry_address'] = mappings[name]
        if d.get('kind') == 'function':
            d['callable_address'] = f"0x{int(mappings[name],16)|1:x}"
        d['status'] = 'STATIC_RECOVERED'
        d['approval_state'] = 'PENDING'
        d['policy'] = 'restricted' if d.get('kind') == 'function' else d.get('policy','restricted')
        d['proof'] = {'static': 'recovered', 'device': 'not_probed',
                      'host_tested': False,
                      'evidence_ids': ['EVID-1108-SYNC-001']}
        d['provenance']['evidence_ids'] = ['EVID-1108-SYNC-001']
        d.pop('promotion', None)
    else:
        # Inventory parity, not an ABI claim. FORBIDDEN is the existing
        # fail-closed status understood by the veneer and Rust generators.
        d.pop('entry_address', None)
        d.pop('callable_address', None)
        d.pop('promotion', None)
        d['status'] = 'FORBIDDEN'
        d['policy'] = 'forbidden'
        d['approval_state'] = 'PENDING'
        d['proof'] = {'static': 'withheld', 'device': 'not_probed',
                      'host_tested': False}
        d['notes'] = ((d.get('notes', '') + ' ') if d.get('notes') else '') + \
                     '11-108 inventory-only placeholder; address withheld until exact-IDB semantic proof.'
    out = OUT / f'{TARGET}.{category}.{name}.json'
    out.write_text(json.dumps(d, indent=2) + '\n')

# Remove stale 11-108 records whose names are not in the 036 inventory.
source_names = {json.loads(p.read_text())['name'] for p in SRC.glob('*.json')}
for p in OUT.glob('*.json'):
    if json.loads(p.read_text()).get('name') not in source_names:
        p.unlink()
print(f'synchronized {len(source_names)} records; mapped={len(mappings)}')
