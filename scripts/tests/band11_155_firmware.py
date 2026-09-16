#!/usr/bin/env python3
"""Execute the independently built .155 loader, Manager and notification paths."""
import struct
import unittest
import band11_notification_firmware as notifications
from band11_arm_bootstrap import Machine
from band11_native_ui_firmware import NativeUI
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2

TARGET = 'xiaomi-band-11-4.100.155'


class Notifications155(notifications.NotificationTests):
    target = TARGET


class Bootstrap155(unittest.TestCase):
    def test_boot_at_two_rebases_and_wrong_version(self):
        for code in (0x1c400020, 0x1c712348):
            m = Machine(code=code, target=TARGET)
            self.assertEqual(m.boot(), 0)
            self.assertEqual(m.register_calls, 2)
            self.assertEqual(m.peak_kernel, 0)
            self.assertEqual(m.word(0x200f5190), 0x0f)
        m = Machine(target=TARGET, fault='wrong_identity')
        self.assertNotEqual(m.boot(), 0)
        self.assertEqual(m.register_calls, 0)

    def test_manager_registration_notification_and_real_native_rows(self):
        m = Machine(target=TARGET)
        self.assertEqual(m.boot(), 0)
        m.finish_access_monitor()
        ui = NativeUI(m)
        fw = notifications.NotificationFirmware(m)
        frame = 0x200d0000
        for command, arg in [(0x4351000a, 0), (0x43510002, 0), (0x43510002, 1), (0x43510002, 2)]:
            m.uc.mem_write(frame, struct.pack('<4I', 0x43504331, command, arg, 0))
            m.uc.reg_write(UC_ARM_REG_R1, frame)
            m.uc.reg_write(UC_ARM_REG_R2, 16)
            self.assertEqual(m.call(m.word(m.fops + 12)), 16)
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
            self.assertEqual((m.widgets[row]['width'], m.widgets[row]['height']), (204, 96))
            self.assertEqual(ui.get(m.word(row + 0x3c), 0, 0x5a), ui.FONT28)
        before = len(m.widgets)
        self.assertEqual(m.call(m.word(page + 0x50), page), 0)
        self.assertEqual(len(m.widgets), before)


if __name__ == '__main__':
    unittest.main(verbosity=2)
