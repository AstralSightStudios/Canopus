"""Offline identity and semantic-policy gates for the exact .177 target."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_band11_installer as installer
import generate_band11_native_config as native

TARGET = 'xiaomi-band-11-4.100.177'
SHA = 'ff74c6f467529963b6afd40669437d8ec39a02e48c10d0893739847b3ba3dc2b'


class Band11Target177Tests(unittest.TestCase):
    def test_exact_identity_and_native_profile(self):
        pack, profile, addresses = native.metadata(TARGET)
        recovery = installer.metadata(TARGET)
        self.assertEqual(pack['firmware_sha256'], SHA)
        self.assertEqual(pack['firmware_build'], 'user-4.100.177-cn-202609241530')
        self.assertEqual(recovery['pmain'], 0x0c6d170c)
        self.assertEqual(recovery['os_execute'], 0x0c7160c0)
        self.assertEqual(recovery['pmain_error_handler'], 0x0c6cd02c)
        self.assertEqual(recovery['io_open'], 0x0c715ce8)
        self.assertEqual(addresses['B11_MPU_BITMAP'], 0x200f4080)
        self.assertEqual(addresses['B11_KMEM_SLOT'], 0x200b0070)
        self.assertEqual(addresses['B11_UMEM_SLOT'], 0x200b2330)
        self.assertEqual(profile['firmware_sha256'], SHA)

    def test_source_semantic_rejections_and_uncertainty_are_preserved(self):
        source = ROOT / 'targets/xiaomi-band-11-4.100.155/symbols'
        target = ROOT / 'targets' / TARGET / 'symbols'
        approved = []
        for path in source.glob('*.json'):
            baseline = json.loads(path.read_text())
            record = json.loads((target / path.name.replace('4.100.155', '4.100.177')).read_text())
            with self.subTest(symbol=record['name']):
                self.assertEqual(record['status'], baseline['status'])
                self.assertEqual(record.get('approval_state'), baseline.get('approval_state'))
                self.assertEqual(record['policy'], baseline['policy'])
                self.assertEqual(record['proof']['device'], 'not_probed')
                self.assertEqual(record['provenance']['firmware_sha256'], SHA)
                if record.get('approval_state') == 'APPROVED':
                    approved.append(record['name'])
        self.assertEqual(approved, ['errno_location'])

    def test_emulation_maps_relocated_data_explicitly(self):
        path = ROOT / 'targets' / TARGET / 'evidence/fw-match/emulation-addresses.json'
        audit = json.loads(path.read_text())
        self.assertEqual(audit['firmware_sha256'], SHA)
        self.assertEqual(audit['data_addresses']['0x200b01c8'], '0x200b0070')
        self.assertEqual(audit['data_addresses']['0x20087630'], '0x200877d4')
        self.assertEqual(audit['data_addresses']['0x200c1574'], '0x200c0f0c')


if __name__ == '__main__':
    unittest.main()
