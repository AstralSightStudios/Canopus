#!/usr/bin/env python3
"""Run the .139 native notification adapter and real notification firmware.

Heap allocation, logging, time, settings, reminder presentation and timers are modeled.
Message normalization, string cloning, list insertion and reminder branching
execute the exact firmware instructions, including the event consumer that
faulted in crash3. This does not verify a real display.
"""
import hashlib
import struct
import unittest

from band11_arm_bootstrap import Machine, ROOT, RES, TARGET
from unicorn import UC_HOOK_CODE, UcError
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_PC

INSERT = 0x0c8f2bb0


def native_symbol(m, name):
    elf = (m.res / f'canopus_supervisor-{m.target}.bin').read_bytes()
    shoff = struct.unpack_from('<I', elf, 32)[0]
    shnum = struct.unpack_from('<H', elf, 48)[0]
    sections = [struct.unpack_from('<10I', elf, shoff + 40 * i) for i in range(shnum)]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = elf[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            label, value, _, _, _, index = struct.unpack_from('<IIIBBH', elf, offset)
            symbols[names[label:].split(b'\0', 1)[0].decode()] = value, index
    value, section = symbols[name]
    anchor, anchor_section = symbols['sup_control_read']
    assert section == anchor_section, 'native function must share the control-read text section'
    return m.word(m.fops + 8) + value - anchor


class NotificationFirmware:
    def __init__(self, m):
        self.m = m
        self.events, self.timers, self.updates = [], [], []
        self.reminders, self.focus_reads = [], []
        m.uc.hook_del(m.firmware_hooks[INSERT])
        m.word(0x20087630, 12)  # Stock LVGL list of per-uid message lists.
        m.word(0x20087634, 0)
        m.word(0x20087638, 0)
        hooks = {
            0x0c351724: self.allocate,
            0x0c3abe20: self.allocate,
            0x0c350474: lambda: 0,
            0x0c34d5ac: self.time,
            0x0c8ec158: self.event,
            0x0c3f0a6e: self.timer,
            0x0c8ed098: self.update,
            0x0c4af67c: lambda: 0,  # settings: auto-screen/wrist-drop off
            0x0c369290: lambda: 0,
            0x0c363838: lambda: 0,
            0x0c363568: lambda: 0,
            0x0c6d467c: lambda: 0,  # DND off
            0x0c8cbfe8: self.present,
            0x1c000200: lambda: 0,  # settings provider callback
        }
        m.word(0x20084ec4, 0x200d8000)
        m.word(0x200d8000 + 44 * 4, 0x1c000201)
        for address, fn in hooks.items():
            actual = m.fw(address) if 0x0c000000 <= address < 0x0d000000 else address
            m.uc.hook_add(UC_HOOK_CODE, m.firmware_call, fn, actual, actual)
        m.uc.hook_add(UC_HOOK_CODE, self.focus_read, None, m.fw(0x0c55fe06), m.fw(0x0c55fe06))

    def focus_read(self, uc, address, size, data):
        self.focus_reads.append(address)

    def present(self):
        self.reminders.append(self.m.reg(0))
        return 0

    def allocate(self):
        m = self.m
        size = m.reg(0)
        p = (m.next_temp + 7) & ~7
        m.next_temp = p + size + 16
        m.allocations[p] = size
        m.uc.mem_write(p, b'\xcc' * size)
        return p

    def time(self):
        self.m.uc.mem_write(self.m.reg(0), struct.pack('<qq', 0x123456789, 0))
        return 0

    def event(self):
        m = self.m
        event, payload, copy = m.reg(0), m.reg(1), m.reg(2)
        assert event == 34 and copy == 1
        assert m.uc.mem_read(payload, 1)[0] == 7
        messages = m.word(payload + 20)
        assert m.word(messages) == 96
        self.events.append(m.word(messages + 4))
        # Synchronous broker dispatch: run the real ntf_event_all_cb and its
        # notifications_message_reminder_start callee, retaining the caller LR.
        m.uc.reg_write(UC_ARM_REG_R0, 0)
        m.uc.reg_write(UC_ARM_REG_PC, m.fw(0x0c559435))
        return None

    def timer(self):
        self.timers.append((self.m.reg(0), self.m.reg(1), self.m.reg(2)))
        return 0

    def update(self):
        self.updates.append(self.m.reg(0))
        return 0

    def stored(self):
        m = self.m
        group = m.word(0x20087634)
        messages = []
        while group:
            message = m.word(group + 4)
            while message:
                messages.append(message)
                message = m.word(message + 100)
            group = m.word(group + 16)
        return messages


class NotificationTests(unittest.TestCase):
    target = TARGET
    @classmethod
    def setUpClass(cls):
        import tomllib
        firmware = (ROOT / 'fwbins' / cls.target / 'vela_ap.bin').read_bytes()
        pack = tomllib.loads((ROOT / 'targets' / cls.target / 'target.toml').read_text())
        assert hashlib.sha256(firmware).hexdigest() == pack['firmware_sha256']

    def machine(self, real=False):
        m = Machine(target=self.target)
        self.assertEqual(m.boot(), 0)
        self.assertEqual(m.notifications, [], 'bootstrap constructor must not notify from NSH')
        m.finish_access_monitor()
        return m, NotificationFirmware(m) if real else None

    def install_manager(self, m):
        frame = 0x200d0000
        m.uc.mem_write(frame, struct.pack('<4I', 0x43504331, 0x43510002, 0, 0))
        m.uc.reg_write(UC_ARM_REG_R1, frame)
        m.uc.reg_write(UC_ARM_REG_R2, 16)
        self.assertEqual(m.call(m.word(m.fops + 12)), 16)

    def test_native_success_paths_and_void_return(self):
        m, _ = self.machine()
        self.install_manager(m)
        self.install_manager(m)
        self.assertEqual(len(m.notifications), 1, 'idempotent install must not notify twice')
        self.assertEqual(m.call(native_symbol(m, 'canopus_manager_native_notify_module_installed')), 0)
        self.assertEqual([n['id'] for n in m.notifications], [0x43414E4F50555301, 0x43414E4F50555302])
        self.assertEqual(m.notifications[0]['body'], 'Canopus 已加载！尽情享受吧～')
        self.assertEqual(m.notifications[1]['body'], '新模块已安装但处于禁用状态。打开 Canopus 管理器即可启用。')
        for message in m.notifications:
            self.assertEqual(message['title'], 'Canopus')
            self.assertEqual(message['source'], 'Canopus')
            self.assertEqual(message['small_icon'], '/data/canopus/manager_icon.bin')
            self.assertEqual(message['large_icon'], message['small_icon'])
            self.assertEqual(message['reminder'], 1)

    def test_wrong_identity_never_calls_notification_firmware(self):
        m, _ = self.machine()
        m.uc.mem_write(m.fw(0x0ca0d216), b'4.100.138')
        self.assertEqual(m.call(native_symbol(m, 'canopus_manager_native_notify_module_installed')), 0xffffffff)
        self.assertEqual(m.notifications, [])

    def assert_firmware_message(self, m, pointer, uid, body):
        self.assertEqual(struct.unpack('<Q', m.uc.mem_read(pointer, 8))[0], uid)
        self.assertEqual(m.string(m.word(pointer + 12)), 'Canopus')
        self.assertEqual(m.string(m.word(pointer + 16)), 'Canopus')
        self.assertEqual(m.string(m.word(pointer + 20)), body)
        for offset in (28, 32):
            self.assertEqual(m.string(m.word(pointer + offset)), '/data/canopus/manager_icon.bin')
        self.assertEqual(struct.unpack('<Q', m.uc.mem_read(pointer + 48, 8))[0], 0x123456789)
        self.assertEqual(bytes(m.uc.mem_read(pointer + 88, 3)), b'\x01\0\0')
        for offset in (64, 68, 72, 76, 80, 84):
            self.assertEqual(m.word(pointer + offset), 0)
        context = m.word(pointer + 92)
        self.assertNotEqual(context, 0)
        self.assertEqual(bytes(m.uc.mem_read(context, 16)), bytes(16))

    def test_real_firmware_inserts_and_starts_both_reminders(self):
        m, fw = self.machine(real=True)
        self.install_manager(m)
        self.install_manager(m)
        self.assertEqual(len(fw.events), 1)
        self.assertEqual(m.call(native_symbol(m, 'canopus_manager_native_notify_module_installed')), 0)
        self.assertEqual(len(fw.events), 2)
        self.assertEqual(len(fw.reminders), 2)
        self.assertEqual(fw.focus_reads, [m.fw(0x0c55fe06)] * 2)
        self.assertEqual(len(fw.updates), 2)
        self.assertEqual(fw.timers, [(0x200c1f08, m.fw(0x0c6a14a5), 500)] * 2)
        stored = fw.stored()
        self.assertEqual(len(stored), 2)
        for index, (uid, body) in enumerate([
            (0x43414E4F50555301, 'Canopus 已加载！尽情享受吧～'),
            (0x43414E4F50555302, '新模块已安装但处于禁用状态。打开 Canopus 管理器即可启用。'),
        ]):
            self.assert_firmware_message(m, fw.events[index], uid, body)
            self.assert_firmware_message(m, stored[1 - index], uid, body)
            for offset in (12, 16, 20, 28, 32):
                self.assertNotEqual(m.word(fw.events[index] + offset), m.word(stored[1 - index] + offset))
        # A second module notice must also notify, even within the same uid group.
        self.assertEqual(m.call(native_symbol(m, 'canopus_manager_native_notify_module_installed')), 0)
        self.assertEqual(len(fw.events), 3)
        self.assertEqual(len(fw.stored()), 3)
        self.assertEqual(len(fw.reminders), 3)
        self.assertNotEqual(m.word(fw.events[0] + 92), m.word(fw.events[1] + 92))
        # The borrowed context survives normalization cleanup and later reads.
        self.assertEqual(m.word(fw.events[1] + 92), m.word(fw.events[2] + 92))

    def test_old_null_context_reproduces_crash3_pc(self):
        m, fw = self.machine(real=True)
        self.install_manager(m)
        message = 0x200d9000
        raw = bytearray(m.uc.mem_read(fw.events[0], 96))
        struct.pack_into('<I', raw, 92, 0)
        m.uc.mem_write(message, bytes(raw))
        with self.assertRaises(UcError):
            m.call(m.fw(INSERT), message)
        self.assertEqual(m.uc.reg_read(UC_ARM_REG_PC), m.fw(0x0c55fe06))

    def test_verified_package_install_notifies_only_after_commit(self):
        from band11_module_load import staged
        package = staged()
        if package is None:
            self.skipTest('no locally staged signed module/receipt')
        module_id, _, _, receipt, elf = package
        if receipt[64:112].split(b'\0', 1)[0].decode() != self.target:
            self.skipTest('staged signed module belongs to a different exact target')
        m, fw = self.machine(real=True)
        m.disk[f'/data/canopus/inbox/{module_id}.cmi'] = receipt
        m.disk[f'/data/canopus/inbox/{module_id}.ko'] = elf
        token = 0x200d2000
        m.uc.mem_write(token, module_id.encode() + b'\0')
        entry = native_symbol(m, 'sup_stage_package')
        def install():
            m.uc.reg_write(UC_ARM_REG_R1, token)
            m.uc.reg_write(UC_ARM_REG_R2, 0)
            return m.call(entry)
        m.disk[f'/data/canopus/inbox/{module_id}.ko'] = elf[:-1] + bytes([elf[-1] ^ 1])
        self.assertEqual(install(), 0xffffffff)
        self.assertEqual(fw.events, [], 'invalid artifact must not notify')
        m.disk[f'/data/canopus/inbox/{module_id}.ko'] = elf
        self.assertEqual(install(), 0)
        self.assertEqual(len(fw.events), 1)
        self.assertIn('新模块已安装', m.string(m.word(fw.events[0] + 20)))
        self.assertEqual(install(), 0xffffffff)
        self.assertEqual(len(fw.events), 1, 'rejected duplicate must not notify')


if __name__ == '__main__':
    unittest.main(verbosity=2)
