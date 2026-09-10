"""Execute .139's actual pmain prefix/loop getter with modeled Lua C APIs.

This checks native loads/stores and control flow, separately from the real
Lua 5.4 C-frame regression. It does not emulate the Lua VM or a device.
"""
from pathlib import Path
import hashlib
import struct
import unittest
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[2]


class FirmwareLua:
    def __init__(self):
        image = (ROOT / 'fwbins/xiaomi-band-11-4.100.139/vela_ap.bin').read_bytes()
        assert hashlib.sha256(image).hexdigest() == '31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74'
        self.u = u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        for base in (0x0c0c0000, 0x2c0c0000):
            u.mem_map(base, (len(image) + 4095) & ~4095)
            u.mem_write(base, image)
        u.mem_map(0x20000000, 0x20000)
        self.registry = {}
        self.stack = []
        self.next_data = 0x20004000
        self.root = 0x20002000
        self.original = (0x20003000, 0x0c6bd3e5, 0x0c6bb995, 0x20003100)
        u.mem_write(self.root, struct.pack('<4I', *self.original))
        self.args = [(4, 0x2ca7633c), (2, 0x20003200), (2, self.root)]
        self.api = {0xc70b4e8:'pointer', 0xc7045f4:'pointer',
                    0xc70b854:'push_string', 0xc70469e:'push_light',
                    0xc7057c8:'rawget', 0xc704542:'type',
                    0xc70bc68:'newdata', 0xc70c020:'rawset',
                    0xc709018:'settop', 0xc720ea8:'memset'}
        u.hook_add(UC_HOOK_CODE, self.hook)

    def value(self, index):
        return self.stack[index - 1 if index > 0 else index]

    def hook(self, u, address, size, _):
        operation = self.api.get(address)
        if not operation:
            return
        r0, r1, r2 = [u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
        index = r1 if r1 < 0x80000000 else r1 - 0x100000000
        result = 0
        if operation == 'pointer':
            result = self.value(index)[1]
        elif operation == 'push_string':
            self.stack.append((4, r1))
        elif operation == 'push_light':
            self.stack.append((2, r1))
        elif operation == 'rawget':
            self.stack.append(self.registry.get(self.stack.pop(), (0, 0)))
            result = self.stack[-1][0]
        elif operation == 'type':
            result = self.value(index)[0]
        elif operation == 'newdata':
            assert r2 == 1
            result = self.next_data
            self.next_data += (r1 + 15) & ~15
            self.stack.append((7, result))
        elif operation == 'rawset':
            value, key = self.stack.pop(), self.stack.pop()
            self.registry[key] = value
        elif operation == 'settop':
            top = index if index >= 0 else len(self.stack) + index + 1
            assert 0 <= top <= len(self.stack)
            del self.stack[top:]
        elif operation == 'memset':
            u.mem_write(r0, bytes([r1]) * r2)
            result = r0
        else:
            raise AssertionError(operation)
        for reg in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12):
            u.reg_write(reg, 0xdeadbeef)
        u.reg_write(UC_ARM_REG_R0, result)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def run(self, entry, end=0x20010000):
        self.u.reg_write(UC_ARM_REG_SP, 0x2001fff0)
        self.u.reg_write(UC_ARM_REG_LR, 0x20010001)
        self.u.reg_write(UC_ARM_REG_R0, 0x20001000)
        self.u.emu_start(entry | 1, end, count=10000)
        assert self.u.reg_read(UC_ARM_REG_PC) == end

    def prefix(self):
        self.run(0xc6d33b0, 0xc6d3470)

    def words(self, pointer, count):
        return struct.unpack('<' + 'I' * count, self.u.mem_read(pointer, count * 4))


class PmainFirmwareTests(unittest.TestCase):
    def test_first_loop_initialization_consumes_root(self):
        m = FirmwareLua()
        m.stack = list(m.args)
        m.run(0xc6c89e4)
        self.assertEqual(m.stack, m.args[:2])
        # The already-created branch is balanced.
        m.stack = list(m.args)
        m.run(0xc6c89e4)
        self.assertEqual(m.stack, m.args)

    def test_cached_root_reentry_isolates_dataman(self):
        m = FirmwareLua()
        m.stack = list(m.args)
        m.prefix()
        self.assertEqual(m.stack, m.args)
        root_key, dataman_key = (4, 0x2cbc9c22), (4, 0x2cbc965f)
        root = m.registry[root_key]
        dataman = m.registry[dataman_key]
        self.assertEqual(m.words(root[1], 4), m.original[:3] + (0,))
        # luaopen_lvgl supplies the destructor in word 4, not dataman.
        m.u.mem_write(root[1] + 12, struct.pack('<I', 0xc6c6ce1))
        saved_root = m.words(root[1], 4)
        saved_dataman = m.words(dataman[1], 2)
        del m.registry[dataman_key]
        m.stack = m.args[:2] + [root]
        m.prefix()
        self.assertEqual(m.words(root[1], 4), saved_root)
        self.assertEqual(m.words(dataman[1], 2), saved_dataman)
        temporary = m.registry[dataman_key]
        self.assertNotEqual(temporary, dataman)
        self.assertEqual(m.words(temporary[1], 2), (0xc6c6ce1, 0))
        m.registry[dataman_key] = dataman
        self.assertEqual(saved_dataman, (m.original[3], 0))


if __name__ == '__main__':
    unittest.main()
