#!/usr/bin/env python3
"""Execute the independently built .155 loader, Manager and notification paths."""
import struct
import sys as _s, pathlib as _p
_s.path.insert(0, str(_p.Path(__file__).resolve().parent))
import cpc2
import unittest
import band11_notification_firmware as notifications
from band11_arm_bootstrap import Machine
from band11_native_ui_firmware import NativeUI, NativeUITests
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2

TARGET = 'xiaomi-band-11-4.100.155'


class Notifications155(notifications.NotificationTests):
    target = TARGET


class NativeUI155(NativeUITests):
    target = TARGET


class Bootstrap155(unittest.TestCase):
    target = TARGET
    def test_boot_at_two_rebases_and_wrong_version(self):
        for code in (0x1c400020, 0x1c712348):
            m = Machine(code=code, target=self.target)
            self.assertEqual(m.boot(), 0)
            self.assertEqual(m.register_calls, 2)
            self.assertEqual(m.peak_kernel, 0)
            self.assertEqual(m.word(m.mpu_bitmap), 0x0f)
        m = Machine(target=self.target, fault='wrong_identity')
        self.assertNotEqual(m.boot(), 0)
        self.assertEqual(m.register_calls, 0)

    def test_manager_registration_notification_and_real_native_rows(self):
        m = Machine(target=self.target)
        self.assertEqual(m.boot(), 0)
        m.finish_access_monitor()
        ui = NativeUI(m)
        fw = notifications.NotificationFirmware(m)
        frame = 0x200d0000
        for command, arg in [(0x4351000a, 0), (0x43510002, 0), (0x43510002, 1), (0x43510002, 2)]:
            request = cpc2.legacy(command, arg)
            m.uc.mem_write(frame, request)
            m.uc.reg_write(UC_ARM_REG_R1, frame)
            m.uc.reg_write(UC_ARM_REG_R2, len(request))
            self.assertEqual(m.call(m.word(m.fops + 12)), len(request))
        self.assertEqual(m.launcher, [0xca])
        self.assertEqual(len(fw.reminders), 1)
        page = m.pages[0]
        m.uc.reg_write(UC_ARM_REG_R1, 0x200b0000)
        m.uc.reg_write(UC_ARM_REG_R2, 0)
        self.assertEqual(m.call(m.word(page + 0x4c), page), 0)
        self.assertEqual(m.call(m.word(page + 0x50), page), 0)
        rows = [p for p, w in m.widgets.items() if w.get('kind') == 'row']
        self.assertTrue(rows)
        for row in rows:
            self.assertEqual((m.widgets[row]['width'], m.widgets[row]['height']), (212, 96))
            self.assertEqual(ui.get(m.word(row + 0x3c), 0, 0x5a), ui.FONT28)
        before = len(m.widgets)
        self.assertEqual(m.call(m.word(page + 0x50), page), 0)
        self.assertEqual(len(m.widgets), before)


if __name__ == '__main__':
    unittest.main(verbosity=2)
