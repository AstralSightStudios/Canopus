#!/usr/bin/env python3
"""Run the .139 native list/theme/update bodies with a modeled LVGL core.

This executes firmware Thumb, including stack arguments, class constructor,
theme selection, font propagation and row layout. Core style storage, object
allocation, animation and drawing are modeled; it is not a display emulator.
"""
import struct
import unittest
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3


class NativeUI:
    ROW = 0x2ca3ed20
    SWITCH = 0x2ca3ecfc
    FONT28 = 0x200e8000
    FONT24 = 0x200e8040

    def __init__(self, machine):
        self.m = m = machine
        self.styles = {0x200c1574: {0x5a: self.FONT28},
                       0x200c1568: {0x5a: self.FONT24}}
        m.word(self.FONT28 + 12, 28)
        m.word(self.FONT24 + 12, 24)
        self.calls = {}
        self.events = {}
        self.forwarded = []
        # Host-only class init shim: call the REAL row constructor, then the
        # REAL theme callback, as lv_obj_class_init_obj does on the device.
        self.init_shim = 0x1c100000
        m.uc.mem_write(self.init_shim, bytes.fromhex(
            '10b5 0446 2146 2068 4368 9847 2146 0020 014b 9847 10bd 00bf')
            + struct.pack('<I', 0x0c5048d9))
        hooks = {
            0xc3806d4: self.class_create, 0xc385490: self.class_init,
            0xc380686: lambda: self.new(m.reg(0)),
            0xc3ab300: lambda: self.new(m.reg(0), kind='label'),
            0xc3ae1d0: lambda: self.new(m.reg(0), kind='image', width=18, height=24),
            0xc89e7dc: self.title, 0xc3b3948: self.text,
            0xc3a9f18: self.style_init, 0xc3a8698: self.style_set,
            0xc378f1e: self.style_get, 0xc3856d0: self.style_add,
            0xc385520: self.style_remove, 0xc38583c: self.styles_clear,
            0xc3872b4: self.local_style, 0xc382620: self.object_style_get,
            0xc3b5750: lambda: self.get(m.reg(0), m.reg(1), 0x58, 0xffffff),
            0xc387ba8: self.size, 0xc387b2e: self.width, 0xc387b76: self.height,
            0xc387788: self.align, 0xc3877b0: self.align_to,
            0xc38404c: lambda: self.flags(True), 0xc3840d8: lambda: self.flags(False),
            0xc3884e0: lambda: self.state(True), 0xc388504: lambda: self.state(False),
            0xc3b3f5c: self.long_mode, 0xc37fc1c: self.event_add,
            0xc3b2c28: self.image_src, 0xc37fb0a: lambda: 1,
            0xc37fbc8: self.event_send,
        }
        # Core refresh/animation machinery is outside the tested ABI boundary.
        for a in (0xc384196, 0xc383278, 0xc380ae0, 0xc3809e2, 0xc384c4c,
                  0xc38400c, 0xc3a9db4, 0xc3a7d6c, 0xc8b9478):
            hooks[a] = lambda: 0
        for a, fn in hooks.items():
            if a in m.firmware_hooks:
                m.uc.hook_del(m.firmware_hooks.pop(a))
            m.firmware_hooks[a] = m.uc.hook_add(UC_HOOK_CODE, m.firmware_call, fn, a, a)
        for a in (0xc50f4e4, 0xc506af8, 0xc5048d8, 0xc50fad0, 0xc50f520,
                  0xc50ec5c, 0xc50eaac, 0xc507140, 0xc506f6c, 0xc4fbe54):
            m.uc.hook_add(UC_HOOK_CODE, self.count, None, a, a)

    def count(self, uc, address, size, data):
        self.calls[address] = self.calls.get(address, 0) + 1

    def new(self, parent, kind='object', cls=0x2ca1082c, width=0, height=0):
        m = self.m
        p = m.next_widget
        m.next_widget += 0x100
        m.uc.mem_write(p, bytes(0x100))
        m.widgets[p] = {'parent': parent, 'kind': kind, 'styles': [], 'local': {}, 'flags': 0x12}
        m.word(p, cls)
        m.word(p + 4, parent)
        self.dimensions(p, width, height)
        return p

    def dimensions(self, p, width=None, height=None):
        m = self.m
        if width is not None:
            m.widgets[p]['width'] = width
            m.word(p + 0x24, m.word(p + 0x1c) + max(1, width) - 1)
        if height is not None:
            m.widgets[p]['height'] = height
            m.word(p + 0x28, m.word(p + 0x20) + max(1, height) - 1)

    def class_create(self):
        m = self.m
        cls = m.reg(0)
        assert cls in (self.ROW, self.SWITCH), hex(cls)
        return self.new(m.reg(1), 'row' if cls == self.ROW else 'switch', cls,
                        0 if cls == self.ROW else 56, 0 if cls == self.ROW else 32)

    def class_init(self):
        m = self.m
        if m.word(m.reg(0)) == self.ROW:
            m.uc.reg_write(UC_ARM_REG_PC, self.init_shim | 1)
            return None
        return 0

    def title(self):
        p = self.new(self.m.reg(0), 'title', width=212, height=68)
        self.m.widgets[p]['text'] = self.m.string(self.m.reg(1))
        return p

    def text(self):
        self.m.widgets[self.m.reg(0)]['text'] = self.m.string(self.m.reg(1)) if self.m.reg(1) else ''
        return 0

    def style_init(self):
        self.styles[self.m.reg(0)] = {}
        return 0

    def style_set(self):
        m = self.m
        self.styles.setdefault(m.reg(0), {})[m.reg(1)] = m.reg(2)
        return 0

    def style_get(self):
        m = self.m
        value = self.styles.get(m.reg(0), {}).get(m.reg(1))
        if value is None:
            return 0
        m.word(m.reg(2), value)
        return 1

    def style_add(self):
        m = self.m
        m.widgets[m.reg(0)]['styles'].append((m.reg(1), m.reg(2)))
        return 0

    def style_remove(self):
        m = self.m
        w = m.widgets[m.reg(0)]
        w['styles'] = [x for x in w['styles'] if x != (m.reg(1), m.reg(2))]
        return 0

    def styles_clear(self):
        self.m.widgets[self.m.reg(0)]['styles'] = []
        self.m.widgets[self.m.reg(0)]['local'] = {}
        return 0

    def local_style(self):
        m = self.m
        m.widgets[m.reg(0)]['local'][m.reg(3), m.reg(1)] = m.reg(2)
        return 0

    def get(self, p, selector, prop, default=0):
        w = self.m.widgets.get(p, {})
        if (selector, prop) in w.get('local', {}):
            return w['local'][selector, prop]
        for style, part in reversed(w.get('styles', [])):
            if part == selector and prop in self.styles.get(style, {}):
                return self.styles[style][prop]
        if prop == 0x5a and w.get('parent'):
            return self.get(w['parent'], 0, prop, default)
        return default

    def object_style_get(self):
        m = self.m
        return self.get(m.reg(0), m.reg(1), m.reg(2), 255 if m.reg(2) == 0x59 else 0)

    def size(self):
        self.dimensions(self.m.reg(0), self.m.reg(1), self.m.reg(2))
        return 0

    def width(self):
        self.dimensions(self.m.reg(0), width=self.m.reg(1))
        return 0

    def height(self):
        self.dimensions(self.m.reg(0), height=self.m.reg(1))
        return 0

    def align(self):
        self.m.widgets[self.m.reg(0)]['align'] = tuple(self.m.reg(i) for i in (1, 2, 3))
        return 0

    def align_to(self):
        m = self.m
        from unicorn.arm_const import UC_ARM_REG_SP
        m.widgets[m.reg(0)]['align_to'] = (m.reg(1), m.reg(2), m.reg(3), m.word(m.uc.reg_read(UC_ARM_REG_SP)))
        return 0

    def flags(self, add):
        m = self.m
        w = m.widgets[m.reg(0)]
        w['flags'] = w['flags'] | m.reg(1) if add else w['flags'] & ~m.reg(1)
        return 0

    def state(self, add):
        m = self.m
        old = struct.unpack('<H', m.uc.mem_read(m.reg(0) + 0x30, 2))[0]
        new = old | m.reg(1) if add else old & ~m.reg(1)
        m.uc.mem_write(m.reg(0) + 0x30, struct.pack('<H', new))
        return 0

    def long_mode(self):
        self.m.widgets[self.m.reg(0)]['long_mode'] = self.m.reg(1)
        return 0

    def event_add(self):
        m = self.m
        self.events.setdefault(m.reg(0), []).append((m.reg(1), m.reg(2)))
        return 0

    def event_send(self):
        self.forwarded.append((self.m.reg(0), self.m.reg(1)))
        return 0

    def image_src(self):
        self.m.widgets[self.m.reg(0)]['src'] = self.m.string(self.m.reg(1))
        return 0


class NativeUITests(unittest.TestCase):
    def setUp(self):
        from band11_arm_bootstrap import Machine
        self.m = Machine()
        self.m.finish_access_monitor()
        self.ui = NativeUI(self.m)
        self.parent = self.ui.new(0, width=212, height=520)

    def call(self, addr, *args):
        m = self.m
        for i, value in enumerate(args[1:4], 1):
            m.uc.reg_write((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i], value)
        for i, value in enumerate(args[4:]):
            m.word(m.stack_top + i * 4, value)
        return m.call(addr, args[0] if args else 0)

    def row(self, trailing, secondary=True):
        m = self.m
        row = self.call(0xc50f4e4, self.parent)
        m.uc.mem_write(0x200e8100, '模块管理\0'.encode())
        m.uc.mem_write(0x200e8140, '已启用\0'.encode())
        self.call(0xc50fad0, row, 0, 0x200e8100, 0x200e8140 if secondary else 0, 0, 0, trailing)
        return row

    def test_real_native_theme_and_chinese_font_propagation(self):
        m, ui = self.m, self.ui
        row = self.row(0)
        self.assertEqual((m.widgets[row]['width'], m.widgets[row]['height']), (204, 96))
        self.assertEqual(ui.get(row, 0, 0x1c), 0x222222)
        self.assertEqual(ui.get(row, 0, 0x0c), 24)
        self.assertEqual(ui.get(row, 0x90000, 0x5a), ui.FONT28)
        self.assertEqual(ui.get(row, 0xa0000, 0x5a), ui.FONT24)
        primary, secondary = m.word(row + 0x3c), m.word(row + 0x40)
        self.assertEqual(ui.get(primary, 0, 0x5a), ui.FONT28)
        self.assertEqual(ui.get(secondary, 0, 0x5a), ui.FONT24)
        self.assertEqual(ui.get(secondary, 0, 0x58), 0x999999)
        self.assertEqual(m.widgets[primary]['text'], '模块管理')
        self.assertEqual(m.widgets[secondary]['text'], '已启用')
        self.assertGreater(m.word(row + 0x48), 0)
        self.assertEqual(ui.calls[0xc506af8], 1)
        self.assertEqual(ui.calls[0xc5048d8], 1)

    def test_real_font_initialization_and_style_apply(self):
        m, ui = self.m, self.ui
        requests = []
        ui.styles.clear()

        def font_factory():
            requests.append((m.string(m.reg(0)), m.reg(1), m.reg(2)))
            self.assertIn(m.reg(1), (24, 26, 28))
            return {28: ui.FONT28, 26: 0x200e8080, 24: ui.FONT24}[m.reg(1)]

        factory = m.uc.hook_add(UC_HOOK_CODE, m.firmware_call, font_factory,
                                0xc4950b0, 0xc4950b0)
        # Execute the stock 28/26/24 font-style initializers. Stop before
        # the next unrelated global style, without replacing either body.
        end = m.uc.hook_add(UC_HOOK_CODE,
                           lambda u, a, s, d: u.reg_write(UC_ARM_REG_PC, m.stop | 1),
                           None, 0xc7e92ee, 0xc7e92ee)
        self.call(0xc7e92ac)
        m.uc.hook_del(end)
        m.uc.hook_del(factory)
        self.assertEqual(requests, [('MiSans-Medium', size, 0) for size in (28, 26, 24)])
        label = ui.new(self.parent, kind='label')
        for _ in range(5):
            self.call(0xc4fbe54, label, 0x200c1568, 255, 0)
        self.assertEqual(ui.get(label, 0, 0x5a), ui.FONT24)
        self.assertEqual(ui.get(label, 0, 0x58), 0xffffff)
        self.assertEqual(len(m.widgets[label]['styles']), 2)

    def test_real_update_reuses_children_and_hides_secondary(self):
        m, ui = self.m, self.ui
        row = self.row(1, secondary=False)
        switch = m.word(row + 0x54)
        self.assertEqual(m.word(switch), ui.SWITCH)
        self.assertEqual(m.word(row + 0x40), 0)
        self.call(0xc50ec5c, row, 0, 0x200e8100, 0x200e8140, 0, 1)
        secondary = m.word(row + 0x40)
        count = len(m.widgets)
        self.assertEqual(ui.get(secondary, 0, 0x5a), ui.FONT24)
        self.assertEqual(m.word(switch + 0x30) & 0xffff, 3)
        for selected in (0, 1, 0, 1):
            self.call(0xc50ec5c, row, 0, 0x200e8100, 0, 0, selected)
            self.assertEqual(m.word(switch + 0x30) & 0xffff, 2 | selected)
            self.assertEqual(len(m.widgets), count)
            self.assertTrue(m.widgets[secondary]['flags'] & 1)
        self.assertEqual(ui.calls[0xc50fad0], 1)

    def test_forward_icon_and_real_row_event_routing(self):
        m, ui = self.m, self.ui
        row = self.row(3)
        trailing = m.word(row + 0x54)
        self.assertEqual(m.widgets[trailing]['src'], '/resource/app/common/icon/forward.bin')
        event = 0x200e8200
        m.uc.mem_write(event, struct.pack('<6I', row, row, 7, 0, 0, 0))
        self.call(0xc506b24, ui.ROW, event)
        self.assertEqual(ui.forwarded, [(trailing, 7)])
        m.word(event + 4, self.parent)
        self.call(0xc506b24, ui.ROW, event)
        self.assertEqual(len(ui.forwarded), 1)


if __name__ == '__main__':
    unittest.main()
