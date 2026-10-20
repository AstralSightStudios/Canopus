#!/usr/bin/env python3
"""Execute the exact .179 loader, Manager, native rows and notification bodies.

Firmware PCs and globals are translated only by the target's independently
derived emulation-addresses.json. A missing mapping fails closed; there is no
.155 fallback.
"""
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import fwlocate
import unittest
import band11_notification_firmware as notifications
import band11_155_firmware as baseline
from band11_native_ui_firmware import NativeUITests

TARGET = 'xiaomi-band-11-4.100.179'

_firmware_file = fwlocate.locate(TARGET) or (_Path(__file__).resolve().parents[2] / 'fwbins' / TARGET / 'vela_ap.bin')
if not (_firmware_file and _firmware_file.is_file()):
    def setUpModule():
        raise unittest.SkipTest(f"no firmware image for {TARGET}")


class Notifications179(notifications.NotificationTests):
    target = TARGET


class NativeUI179(NativeUITests):
    target = TARGET


class Bootstrap179(baseline.Bootstrap155):
    target = TARGET

    def test_exact_bitmap_and_firmware_identity(self):
        import hashlib
        import tomllib
        from band11_arm_bootstrap import Machine, ROOT
        pack = tomllib.loads((ROOT / 'targets' / self.target / 'target.toml').read_text())
        image = fwlocate.locate(self.target) or ROOT / 'fwbins' / self.target / 'vela_ap.bin'
        self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), pack['firmware_sha256'])
        self.assertEqual(pack['firmware_build'], 'user-4.100.179-cn-202609302100')
        m = Machine(target=self.target)
        self.assertEqual(m.mpu_bitmap, 0x200f4080)
        self.assertEqual(m.word(m.mpu_bitmap), 0x0f)
        with self.assertRaises(KeyError):
            m.fw(0xdead0000)


if __name__ == '__main__':
    unittest.main(verbosity=2)
