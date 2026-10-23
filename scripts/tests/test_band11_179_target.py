"""Offline identity and trust gates for the exact .179 target."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_band11_installer as installer

TARGET = 'xiaomi-band-11-4.100.179'
SHA = '307adabe0ff61c77be73d045a8260e279cddd131c7a535a2678eb34199278a2b'


class Band11Target179Tests(unittest.TestCase):
    def test_exact_identity_and_native_profile(self):
        pack, profile, addresses = installer.native_metadata(TARGET)
        recovery = installer.metadata(TARGET)
        self.assertEqual(pack['firmware_sha256'], SHA)
        self.assertEqual(pack['firmware_build'], 'user-4.100.179-cn-202609302100')
        self.assertEqual(recovery['pmain'], 0x0c6d178c)
        self.assertEqual(recovery['os_execute'], 0x0c716140)
        self.assertEqual(recovery['pmain_error_handler'], 0x0c6cd0ac)
        self.assertEqual(recovery['io_open'], 0x0c715d68)
        self.assertEqual(addresses['B11_MPU_BITMAP'], 0x200f4080)
        self.assertEqual(addresses['B11_KMEM_SLOT'], 0x200b0070)
        self.assertEqual(addresses['B11_UMEM_SLOT'], 0x200b2330)
        self.assertEqual(profile['firmware_sha256'], SHA)

    def test_every_155_record_is_ported_and_nothing_is_inherited(self):
        source = ROOT / 'targets/xiaomi-band-11-4.100.155/symbols'
        target = ROOT / 'targets' / TARGET / 'symbols'
        unported = {
            f'{TARGET}.private_abi.native_font_{name}.json'
            for name in ('create', 'release', 'wait')
        }
        self.assertEqual(
            sorted(p.name.replace('4.100.155', '4.100.179')
                   for p in source.glob('*.json')
                   if p.name.replace('4.100.155', '4.100.179') not in unported),
            sorted(p.name for p in target.glob('*.json')))
        for path in source.glob('*.json'):
            if path.name.replace('4.100.155', '4.100.179') in unported:
                continue
            baseline = json.loads(path.read_text())
            record = json.loads((target / path.name.replace('4.100.155', '4.100.179')).read_text())
            with self.subTest(symbol=record['name']):
                self.assertEqual(record['kind'], baseline['kind'])
                self.assertEqual(record.get('prototype'), baseline.get('prototype'))
                self.assertEqual(record['proof']['device'], 'not_probed')
                self.assertEqual(record['provenance']['firmware_sha256'], SHA)
                self.assertNotEqual(record.get('approval_state'), 'APPROVED')
                self.assertNotIn('promotion', record)
                if baseline['policy'] == 'forbidden' or baseline.get('approval_state') == 'REJECTED':
                    # A rejection survives the port: address correspondence never reverses it.
                    self.assertEqual(record['approval_state'], 'REJECTED')
                    self.assertNotIn('callable_address', record)
                if baseline['status'] == 'CANDIDATE':
                    self.assertEqual(record['status'], 'CANDIDATE')

    def test_emulation_maps_relocated_data_explicitly(self):
        audit = json.loads((ROOT / 'targets' / TARGET / 'evidence/fw-match/emulation-addresses.json').read_text())
        self.assertEqual(audit['firmware_sha256'], SHA)
        self.assertEqual(audit['data_addresses']['0x200b01c8'], '0x200b0070')
        self.assertEqual(audit['data_addresses']['0x200f5190'], '0x200f4080')
        self.assertEqual(audit['data_addresses']['0x20087630'], '0x200877d4')
        self.assertEqual(audit['data_addresses']['0x200c1574'], '0x200c0f0c')

    def test_wrong_matcher_pick_was_corrected(self):
        overrides = json.loads((ROOT / 'targets' / TARGET / 'evidence/fw-match/manual-overrides.json').read_text())
        self.assertEqual(overrides['font_reload_metrics_destroy']['entry'], '0xc39fcfc')
        record = json.loads((ROOT / 'targets' / TARGET / 'symbols' /
                             f'{TARGET}.private_abi.font_reload_metrics_destroy.json').read_text())
        self.assertEqual(record['entry_address'], '0xc39fcfc')


if __name__ == '__main__':
    unittest.main()
