import importlib.util
from pathlib import Path
import struct
import unittest

try:
    import capstone
except ImportError:
    capstone = None


@unittest.skipUnless(capstone, 'capstone is required for Thumb fingerprints')
class FingerprintTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('fingerprints', Path(__file__).resolve().parents[1] / 'fingerprint_corpus.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
        self.md.detail = True

    def fp(self, code, address=0x0c0c0000, tail=b''):
        start = address - 0x0c0c0000
        image = bytes(start) + bytes.fromhex(code) + tail
        fn = {'addr': hex(address), 'block_offs': [{'off': 0, 'size': len(bytes.fromhex(code))}]}
        return self.module.fingerprint(fn, image, self.md)

    def test_literal_relocation_keeps_ref_and_memory_field_offsets(self):
        # LDR r0,[pc] reads the relocated literal just after the 4-byte body.
        a = self.fp('00487047', tail=struct.pack('<I', 0x200b0000))
        b = self.fp('00487047', 0x0c0c0020, struct.pack('<I', 0x200c0000))
        self.assertEqual(a['sha256'], b['sha256'])
        self.assertEqual(a['refs'][0]['addr'], '0x200b0000')
        self.assertEqual(b['refs'][0]['addr'], '0x200c0000')
        # The exact crash3 field load ldrb r3,[r5,#13] must not normalize to +12.
        self.assertNotEqual(self.fp('6b7b7047')['sha256'], self.fp('2b7b7047')['sha256'])

    def test_constants_and_incomplete_decoding_do_not_become_matches(self):
        self.assertNotEqual(self.fp('00487047', tail=struct.pack('<I', 16))['sha256'],
                            self.fp('00487047', tail=struct.pack('<I', 96))['sha256'])
        self.assertIsNone(self.fp('00f0'))

