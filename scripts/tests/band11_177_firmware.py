#!/usr/bin/env python3
"""Execute exact .177 loader, Manager, native rows and notification bodies.

Firmware PCs/classes are translated only by the independently recovered target
emulation-addresses.json. Missing mappings fail closed; no .155 PC fallback.
"""
import unittest
import band11_notification_firmware as notifications
import band11_155_firmware as baseline

TARGET = 'xiaomi-band-11-4.100.177'


class Notifications177(notifications.NotificationTests):
    target = TARGET


class Bootstrap177(baseline.Bootstrap155):
    target = TARGET

    def test_exact_bitmap_and_firmware_identity(self):
        import hashlib
        import tomllib
        from band11_arm_bootstrap import Machine, ROOT
        pack = tomllib.loads((ROOT / 'targets' / self.target / 'target.toml').read_text())
        image = ROOT / 'fwbins' / self.target / 'vela_ap.bin'
        self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), pack['firmware_sha256'])
        self.assertEqual(pack['firmware_build'], 'user-4.100.177-cn-202609241530')
        m = Machine(target=self.target)
        self.assertEqual(m.mpu_bitmap, 0x200f4080)
        self.assertEqual(m.word(m.mpu_bitmap), 0x0f)
        with self.assertRaises(KeyError):
            m.fw(0xdead0000)


if __name__ == '__main__':
    unittest.main(verbosity=2)
