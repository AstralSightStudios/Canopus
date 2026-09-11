#!/usr/bin/env python3
"""Execute additional .139 firmware bodies against the production installer.

Allocation, hash tables, notifications and peripheral effects remain modeled.
The command/cache loops and app/page/Launcher field accesses are real ARM.
"""
import hashlib
import struct
import unittest
from band11_arm_bootstrap import Machine, ROOT, TARGET
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_READ
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3


class FirmwareIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.firmware = (ROOT / 'fwbins' / TARGET / 'vela_ap.bin').read_bytes()
        assert hashlib.sha256(cls.firmware).hexdigest() == '31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74'

    def hook(self, m, address, fn):
        return m.uc.hook_add(UC_HOOK_CODE, m.firmware_call, fn, address, address)

    def test_real_lua_io_policy_allows_public_installer_path(self):
        m = Machine()
        path = 0x200d4000
        # Model successful canonicalization. The firmware's actual prefix
        # matcher, deny table, strlen and strncmp execute unchanged.
        def canonicalize():
            data = m.string(m.reg(0)).encode() + b'\0'
            m.uc.mem_write(m.reg(1), data)
            return m.reg(1)
        self.hook(m, 0x0c352af0, canonicalize)
        for name, denied in [('/canopus/install', 0), ('/data/canopus/inbox/module.ko', 0),
                             ('/dev/canopus', 1), ('/dev', 1), ('/proc/x', 1), ('/sys/x', 1)]:
            m.uc.mem_write(path, name.encode() + b'\0')
            self.assertEqual(m.call(0x0c6cfb34, path), denied, name)

    def test_system_uses_4096_byte_command_stack_and_waits(self):
        m = Machine()
        command = 0x200d4000
        m.uc.mem_write(command, b'exec 0x0c91e543\0')
        events = []
        def sched_getparam():
            m.word(m.reg(1), 100)
            return 0
        self.hook(m, 0x0c357274, sched_getparam)
        self.hook(m, 0x0c35550c, lambda: 0)

        def spawn():
            self.assertEqual(m.string(m.reg(0)), 'system')
            self.assertEqual(m.reg(1), 0x0c36c743)
            attr = m.reg(3)
            self.assertEqual(m.word(attr + 12), 4096)
            self.assertEqual(m.word(attr) & 0xffff, 0x6404)
            from unicorn.arm_const import UC_ARM_REG_SP
            argv = m.word(m.uc.reg_read(UC_ARM_REG_SP))
            self.assertEqual(m.string(m.word(argv)), '-c')
            self.assertEqual(m.word(argv + 4), command)
            self.assertEqual(m.word(argv + 8), 0)
            events.append('spawn')
            return 42

        def wait():
            self.assertEqual(m.reg(0), 42)
            self.assertEqual(m.reg(2), 0)
            m.word(m.reg(1), 0)
            events.append('wait')
            return 42

        self.hook(m, 0x0c8bbdac, spawn)
        self.hook(m, 0x0c359c64, wait)
        self.assertEqual(m.call(0x0c370774, command), 0)
        self.assertEqual(events, ['spawn', 'wait'])

    def test_real_initial_state_clears_thread_unprivileged_bit(self):
        # task_init -> task_setup (0xc355990) -> up_initial_state. The exact
        # firmware clears CONTROL.nPRIV even for ordinary type-0 system tasks.
        for caller_control in (0, 2, 3):
            m = Machine()
            m.control = caller_control
            tcb, stack = 0x200d1000, 0x3c700000
            m.word(tcb + 0x2c, 42)
            m.word(tcb + 0x34, 0x0c35a345)
            m.word(tcb + 0x40, 0x8000)
            m.word(tcb + 0x60, 4096)
            m.word(tcb + 0x64, stack)
            m.word(tcb + 0x68, stack)
            m.call(0x0c329ee8, tcb)
            frame = m.word(tcb + 0x9c)
            self.assertEqual(frame, stack + 4096 - 0xe0)
            self.assertEqual(m.word(frame + 0x28), caller_control & ~1)
            self.assertEqual(m.word(frame + 0x2c), 0xffffffed)
            self.assertEqual(m.word(frame + 0x70), stack)
            self.assertEqual(m.word(frame + 0x90), 0x0c35a344)
            self.assertEqual(m.word(frame + 0x94), 0x01000000)

    def test_real_mw_and_relocated_cache_range_functions(self):
        m = Machine()
        m.uc.mem_map(0x07ffa000, 0x4000)
        output, accesses = [], []
        state, argv, arg = 0x200d1000, 0x200d2000, 0x200d2080
        printer = 0x1c000200
        m.word(state + 0x1c, printer | 1)
        m.uc.mem_write(argv + 0x40, b'mw\0')
        m.uc.mem_write(argv + 0x60, b'4\0')
        m.word(argv, argv + 0x40)
        m.word(argv + 4, arg)
        m.word(argv + 8, argv + 0x60)
        m.word(argv + 12, 0)

        def printf():
            fmt = m.string(m.reg(1))
            if fmt == '  %p = 0x%08lx':
                text = f'  0x{m.reg(2):08x} = 0x{m.reg(3):08x}'
            elif fmt == ' -> 0x%08lx':
                text = f' -> 0x{m.reg(2):08x}'
            else:
                self.assertEqual(fmt, '\n')
                text = fmt
            output.append(text)
            return len(text)

        self.hook(m, printer, printf)

        def observe(u, kind, address, size, value, cookie):
            self.assertEqual(size, 4)
            accesses.append(('read' if kind == UC_MEM_READ else 'write', address, value))

        m.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, observe, None, 0x07ffa000, 0x07ffdfff)

        def mw(address, value=None):
            output.clear()
            text = f'{address:08x}' if value is None else f'{address:08x}={value:08x}'
            m.uc.mem_write(arg, text.encode() + b'\0')
            m.uc.reg_write(UC_ARM_REG_R1, 3 if value is None else 2)
            m.uc.reg_write(UC_ARM_REG_R2, argv)
            self.assertEqual(m.call(0x0c372834, state), 0)
            return ''.join(output)

        self.assertEqual(mw(0xe000ed94), '  0xe000ed94 = 0x00000005\n')
        self.assertEqual(mw(0x07ffc080, 0x3c400020), '  0x07ffc080 = 0x00000000 -> 0x3c400020\n')
        self.assertEqual([a[:2] for a in accesses], [('read', 0x07ffc080), ('write', 0x07ffc080), ('read', 0x07ffc080)])

        # Cover all possible 8-byte TString alignments, including partial end
        # lines. Compare Lua's line addresses with actual relocated HAL loops.
        for offset in (0, 8, 16, 24):
            mailbox = 0x3c400000 + offset
            code = mailbox + 32
            size = len((ROOT / 'watchfaces/canopus-installer-prod/xiaomi-band-11' / f'canopus_stage1-{TARGET}.bin').read_bytes())
            for cache_id, entry, start, count, port in (
                (1, 0x00275814, mailbox, size + 32, 0x07ffc080),
                (0, 0x002757bc, (code - 0x20000000) & ~31,
                 ((code - 0x20000000 + size + 31) & ~31) - ((code - 0x20000000) & ~31), 0x07ffa084),
            ):
                accesses.clear()
                m.uc.reg_write(UC_ARM_REG_R1, start)
                m.uc.reg_write(UC_ARM_REG_R2, count)
                m.call(entry, cache_id)
                self.assertEqual([(a, v) for op, a, v in accesses if op == 'write'],
                                 [(port, line) for line in range(start & ~31, ((start + count - 1) & ~31) + 1, 32)])

    def test_cache_fixed_entries_through_real_nsh_preserve_configuration(self):
        m = Machine()
        m.control = 2
        m.uc.mem_map(0x07ffa000, 0x4000)
        m.uc.mem_write(0x07ffa000, b'\xa5' * 0x4000)
        m.word(0x07ffa000, 1)
        m.word(0x07ffc000, 1)
        before = bytes(m.uc.mem_read(0x07ffa000, 0x4000))
        # cmd_exec's printf returns 17 and clobbers caller-saved registers.
        # These leaf entry points establish every register they consume.
        self.assertEqual(m.nsh_exec(0x0c93021a, dispatch=True), 0)
        self.assertEqual(m.nsh_exec(0x0c91e542, dispatch=True), 0)
        self.assertEqual(m.nsh_exec(0x0c0c181e, dispatch=True), 0)
        self.assertEqual(m.nsh_exec(0x0c91e542, dispatch=True), 0)
        expected = bytearray(before)
        struct.pack_into('<I', expected, 0x2034, 1)  # D clean all
        struct.pack_into('<I', expected, 0x0038, 1)  # enabled I clean/invalidate all
        self.assertEqual(bytes(m.uc.mem_read(0x07ffa000, 0x4000)), bytes(expected))
        self.assertEqual(m.word(0xe000ed94), 5)
        self.assertEqual(m.word(0x200f5190), 0x0f)

    def test_real_app_page_and_launcher_registration(self):
        m = Machine()
        self.assertEqual(m.boot(), 0)
        m.finish_access_monitor()  # known Unicorn Thumb ITSTATE hook defect
        for address in (0x0c6ab350, 0x0c6ac54c, 0x0c547b54):
            m.uc.hook_del(m.firmware_hooks[address])
        # Original startup data contains the page-manager vtable. Initialize
        # only that table and explicit empty runtime lists, not arbitrary RAM.
        offset = 0x0cc4be64 - 0x0c0c0000 + 0x20084e8c - 0x2007dcc8
        m.uc.mem_write(0x20084e8c, self.firmware[offset:offset + 28])
        head, app_vtable = 0x200c1000, 0x200c1100
        m.word(0x20084ea8, head)
        m.word(head, head)
        m.word(head + 4, head)
        m.word(0x20084ea8 + 0x1c, app_vtable)
        m.word(app_vtable + 4, 0x0c6ac54d)
        m.word(app_vtable + 0x34, 0x1c000221)
        self.hook(m, 0x1c000220, lambda: 0)
        cursor = 0x3c600000
        def allocate(size):
            nonlocal cursor
            p = cursor
            cursor += (size + 7) & ~7
            m.uc.mem_write(p, b'\xcc' * size)
            return p
        self.hook(m, 0x0c351724, lambda: allocate(m.reg(0)))
        def duplicate():
            data = m.string(m.reg(0)).encode() + b'\0'
            p = allocate(len(data))
            m.uc.mem_write(p, data)
            return p
        self.hook(m, 0x0c351b2a, duplicate)
        self.hook(m, 0x0c6db3aa, lambda: allocate(32))  # map creation
        self.hook(m, 0x0c6d32c4, lambda: 0)  # map insertion
        self.hook(m, 0x0c350474, lambda: 0)  # logging
        notifications, records = [], []
        def notify():
            notifications.append(m.reg(0))
            return 0
        self.hook(m, 0x0c8ec158, notify)
        self.hook(m, 0x0c3a46a2, lambda: allocate(28))  # list record allocation
        def publish():
            self.assertEqual(m.reg(0), 2)
            records.append(m.reg(1))
            return 0
        self.hook(m, 0x0c5454a4, publish)
        self.hook(m, 0x0c545be0, lambda: 0)  # list positioning
        self.hook(m, 0x0c547668, lambda: 0)  # persistence/refresh

        frame = 0x200d0000
        m.uc.mem_write(frame, struct.pack('<4I', 0x43504331, 0x43510002, 0, 0))
        m.uc.reg_write(UC_ARM_REG_R1, frame)
        m.uc.reg_write(UC_ARM_REG_R2, 16)
        self.assertEqual(m.call(m.word(m.fops + 12)), 16)
        app = m.call(0x0c6ac54c, 0xca)
        self.assertNotEqual(app, 0)
        self.assertEqual(m.string(m.word(app + 12)), 'com.canopus.manager')
        self.assertEqual(m.word(app + 0x48) & 1, 0)
        pages = m.word(app + 0x38)
        self.assertEqual(m.word(pages) & 0xffff, 3)
        for i in range(3):
            page = m.word(pages + 8 + 4 * i)
            self.assertEqual(m.word(page + 0x14), 0xca0000 + i)
            self.assertEqual(m.call(0x0c695a44, 0xca0000 + i), page)
            self.assertEqual(m.word(page + 0x2c), 0x20084e8c)
            for off in (0x34, 0x4c, 0x50, 0x5c, 0x64):
                self.assertEqual(m.word(page + off) & 1, 1)
            self.assertEqual(bytes(m.uc.mem_read(page + 0x28, 3)), b'\x04\x04\x02')
        self.assertEqual(len(records), 1)
        record = records[0]
        self.assertEqual(m.word(record) & 0xffff, 0xca)
        self.assertEqual(m.string(m.word(record + 4)), 'Canopus 管理器')
        self.assertEqual(m.string(m.word(record + 8)), 'com.canopus.manager')
        self.assertEqual(m.string(m.word(record + 12)), '/data/canopus/manager_icon.bin')
        self.assertIn(0x10000, notifications)


if __name__ == '__main__':
    unittest.main()
