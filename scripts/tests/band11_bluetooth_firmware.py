#!/usr/bin/env python3
"""Run recovered .139 Bluetooth bodies; kernel scheduling/allocation are modeled.

These tests establish selected ABI facts, not radio or complete audio support.
The exact-target private backend is additionally tested by band11_private_backend.py.
"""
import hashlib
import struct
import unittest

from band11_arm_bootstrap import Machine, ROOT, TARGET
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3


class BluetoothFirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        firmware = (ROOT / 'fwbins' / TARGET / 'vela_ap.bin').read_bytes()
        assert hashlib.sha256(firmware).hexdigest() == '31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74'

    def setUp(self):
        self.m = m = Machine()
        m.finish_access_monitor()
        self.events = []
        self.cursor = 0x3c610000
        self.allocations = {}
        self.hook(0x0c351724, lambda: self.allocate())
        m.uc.hook_del(m.firmware_hooks[0x0c34cd2c])
        self.hook(0x0c34cd2c, self.free)
        self.hook(0x0c88e548, lambda: self.event('lock'))
        self.hook(0x0c88e554, lambda: self.event('post'))
        self.hook(0x0c395c74, lambda: 1)
        m.word(0x200be880, 3)
        m.word(0x200be8f4, 0x200d1000)
        m.word(0x200be8e8, 0x200d1020)

    def hook(self, address, fn):
        return self.m.uc.hook_add(UC_HOOK_CODE, self.m.firmware_call, fn, address, address)

    def event(self, name):
        self.events.append((name, self.m.reg(0)))
        return 0

    def allocate(self):
        size = self.m.reg(0)
        p = self.cursor
        self.cursor += (size + 7) & ~7
        self.allocations[p] = size
        self.m.uc.mem_write(p, b'\xcc' * size)
        return p

    def free(self):
        p = self.m.reg(0)
        if p:
            self.assertIn(p, self.allocations)
            del self.allocations[p]
        self.events.append(('free', p))
        return 0

    def call(self, address, *args):
        args = list(args) + [0] * max(0, 4 - len(args))
        for reg, value in zip((UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args[1:4]):
            self.m.uc.reg_write(reg, value)
        for i, value in enumerate(args[4:]):
            self.m.word(self.m.stack_top + i * 4, value)
        return self.m.call(address, args[0])

    def test_external_queue_lock_wake_and_full_ring_growth(self):
        m = self.m
        ring = 0x3c600000
        m.word(0x200be8f0, ring)
        for n in range(21):
            self.assertEqual(self.call(0x0c863bc4, 0x200d4000, 0x1c000201,
                                      0x1c000221, 0x12340000 + n, 7), 0)
        following = m.word(ring)
        self.assertIn(following, self.allocations)
        self.assertEqual(self.allocations[following], 408)
        for n in range(21):
            slot = ring + 8 + n * 20 if n < 20 else following + 8
            self.assertEqual(struct.unpack('<4I', m.uc.mem_read(slot, 16)),
                             (0x200d4000, 0x12340000 + n, 0x1c000201, 0x1c000221))
            self.assertEqual(m.uc.mem_read(slot + 16, 1), b'\x07')
        self.assertEqual(self.events.count(('lock', 0x200d1000)), 21)
        self.assertEqual(self.events.count(('post', 0x200d1000)), 21)
        self.assertEqual(self.events.count(('post', 0x200d1020)), 1)

    def test_uninitialized_queue_passes_argument_in_r2_to_cancel(self):
        m = self.m
        m.word(0x200be8f4, 0)
        observed = []
        def cancel():
            observed.append((m.reg(0), m.reg(1), m.reg(2)))
            return 55
        self.hook(0x1c000220, cancel)
        self.assertEqual(self.call(0x0c863bc4, 0x200d4000, 0x1c000201,
                                  0x1c000221, 0x12345678, 7), 55)
        self.assertEqual(observed, [(0, 221, 0x12345678)])
        self.assertEqual(self.events, [])

    def test_timer_six_arguments_and_cancel_returns_owned_argument(self):
        m = self.m
        def timer():
            self.assertEqual(m.reg(0), 17)
            return 0x87654321
        self.hook(0x0c88e5f0, timer)
        self.hook(0x0c88e5d4, lambda: self.event('cancel'))
        handle = self.call(0x0c8c7ff0, 0x200d4000, 17, 3, 0x1c000201,
                           0x12345678, 0x55667788)
        self.assertEqual(handle, 0x87654321)
        record = m.word(0x200be884)
        self.assertEqual([m.word(record + n) for n in (0, 4, 8, 12, 16)],
                         [0x55667788, 0x200d4000, 0x1c000201, 0x12345678, handle])
        self.assertEqual(m.uc.mem_read(record + 20, 1), b'\x03')
        # Unlike .043, cancellation takes the handle value and RETURNS its
        # owned argument. The caller must clear its handle and free that token.
        self.assertEqual(self.call(0x0c863010, handle), 0x12345678)
        self.assertIn(('cancel', handle), self.events)
        self.assertIn(('free', record - 8), self.events)
        self.assertNotIn(('free', 0x12345678), self.events)
        self.assertEqual(m.word(0x200be884), 0)
        self.assertEqual(self.call(0x0c863010, handle), 0)

    def test_l2cap_connect_uses_band11_channel_cid_offset(self):
        m = self.m
        request, link, channel = 0x200d4000, 0x200d4200, 0x200d4400
        m.word(request + 12, 0x1c000241)
        m.uc.mem_write(request + 16, b'ABCDEF\0\0')
        m.uc.mem_write(request + 2, struct.pack('<H', 0x19))
        m.uc.mem_write(channel + 116, struct.pack('<H', 0x4242))
        m.word(channel + 20, 0x1c000241)
        self.hook(0x0c871510, lambda: link)
        self.hook(0x0c86d90c, lambda: channel)
        self.hook(0x0c86a304, lambda: 0)
        self.hook(0x0c86d7d0, lambda: 0)
        observed = []
        def confirm():
            observed.append((m.reg(0), m.reg(1)))
            self.assertEqual(struct.unpack('<H', m.uc.mem_read(request, 2))[0], 0x4242)
            return 0
        self.hook(0x1c000240, confirm)
        self.call(0x0c871568, 0x200d4600, 1, request)
        self.assertEqual(observed, [(2, request)])
        self.assertEqual(m.uc.mem_read(request + 23, 1), b'\x02')

    def test_sdp_attribute_grows_preserves_prefix_and_copies_value(self):
        m = self.m
        builder = self.call(0x0c862dee, 8)
        source = 0x200d4000
        m.uc.mem_write(source, b'ABCDE123')
        data = self.call(0x0c86f140, builder, 0x100, 0, 5, source)
        self.assertEqual(bytes(m.uc.mem_read(data, 5)), b'ABCDE')
        data = self.call(0x0c86f140, builder, 0x100, 3, 3, source + 5)
        self.assertEqual(bytes(m.uc.mem_read(data - 3, 6)), b'ABC123')
        record = m.word(builder)
        self.assertEqual(struct.unpack('<HH', m.uc.mem_read(record, 4)), (0x100, 6))


if __name__ == '__main__':
    unittest.main()
