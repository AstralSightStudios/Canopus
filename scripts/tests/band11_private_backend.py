#!/usr/bin/env python3
"""Compile actual .139 Rust adapters and execute against firmware Thumb in Unicorn.

Allocator, kernel synchronization, socket transport and LVGL core are modeled;
no radio, display, or physical concurrency result is implied by these tests.
"""
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import unittest

from band11_bluetooth_firmware import BluetoothFirmwareTests
from band11_arm_bootstrap import ROOT
from band11_native_ui_firmware import NativeUI


def build_backend():
    out = ROOT / 'build/band11-private-tests'
    out.mkdir(parents=True, exist_ok=True)
    sdk = ROOT / 'sdk/rust'
    triple = 'thumbv8m.main-none-eabi'
    subprocess.run(['cargo', 'build', '--manifest-path', str(sdk / 'Cargo.toml'),
                    '--release', '--target', triple, '-p', 'canopus-target-private',
                    '--no-default-features', '--features', 'target-xiaomi-band-11-4-100-139'], check=True)
    deps = sdk / 'target' / triple / 'release'
    source = ROOT / 'scripts/tests/band11_private_backend.rs'
    archive = out / 'backend.a'
    subprocess.run(['rustc', str(source), '--edition', '2024', '--crate-type', 'staticlib',
                    '--target', triple, '-C', 'opt-level=z', '-C', 'panic=abort',
                    '--extern', 'canopus_target_private=' + str(deps / 'libcanopus_target_private.rlib'),
                    '-L', 'dependency=' + str(deps / 'deps'), '-o', str(archive)], check=True)
    exports = re.findall(r'fn (test_\w+)\(', source.read_text())
    script = out / 'backend.ld'
    script.write_text('ENTRY(test_identity)\nSECTIONS { . = 0x1c200000; '
                      '.text : { *(.text*) } .rodata : { *(.rodata*) } '
                      '.data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    linker = os.environ.get('LD_LLD') or shutil.which('ld.lld')
    if not linker:
        raise RuntimeError('ld.lld required (or set LD_LLD)')
    elf = out / 'backend.elf'
    subprocess.run([linker, '-T', str(script), '--gc-sections', '-o', str(elf),
                    *['--undefined=' + n for n in exports], str(archive)], check=True)
    data = elf.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01'
    shoff = struct.unpack_from('<I', data, 32)[0]
    shsize, shnum = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, shoff + n * shsize) for n in range(shnum)]
    symbols = {}
    loads = []
    for s in sections:
        _, kind, flags, addr, off, size, link, _, _, stride = s
        if flags & 2 and size:
            loads.append((addr, bytes(size) if kind == 8 else data[off:off + size]))
        if kind == 2:
            strings = sections[link]
            names = data[strings[4]:strings[4] + strings[5]]
            for i in range(off, off + size, stride):
                name, value, _, _, _, _ = struct.unpack_from('<IIIBBH', data, i)
                n = names[name:].split(b'\0', 1)[0].decode()
                if n in exports:
                    symbols[n] = value
    assert len(symbols) == len(exports)
    return loads, symbols


class PrivateBackendTests(BluetoothFirmwareTests):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.loads, cls.symbols = build_backend()

    def setUp(self):
        super().setUp()
        for address, data in self.loads:
            self.m.uc.mem_write(address, data)

    def rust(self, name, *args):
        return self.call(self.symbols['test_' + name], *args)

    def test_compiled_identity_rejects_wrong_firmware(self):
        self.assertEqual(self.rust('identity'), 0)
        self.m.uc.mem_write(0x0ca0d216, b'4.100.138')
        self.assertNotEqual(self.rust('identity'), 0)

    def test_compiled_buffer_matches_stock_and_checks_overflow_oom(self):
        m = self.m
        for n, h in [(0, 0), (16, 0), (1200, 12), (65527, 4)]:
            a = self.rust('buffer', n, h)
            b = self.call(0xc862ed2, n, h)
            size = self.allocations[a]
            self.assertEqual(size, self.allocations[b])
            self.assertEqual(m.uc.mem_read(a, size), m.uc.mem_read(b, size))
            self.rust('free', a)
            self.rust('free', b)
        before = dict(self.allocations)
        self.assertEqual(self.rust('buffer', 65535, 0), 0)
        self.assertEqual(self.rust('buffer', 0, 65535), 0)
        self.assertEqual(self.allocations, before)
        self.allocate = lambda: 0
        self.assertEqual(self.rust('buffer', 16, 4), 0)
        self.assertEqual(self.allocations, before)

    def test_compiled_completion_reads_firmware_event_not_channel(self):
        m = self.m
        channel, link = 0x200d4000, 0x200d4200
        m.word(channel, link)
        m.word(channel + 20, 0x1c000241)
        m.uc.mem_write(link + 16, b'ABCDEF\0\0')
        m.uc.mem_write(channel + 96, struct.pack('<H', 1008))
        m.uc.mem_write(channel + 112, struct.pack('<4H', 0x19, 0, 0x4242, 0x4141))
        # Already opened channel: real transition body can return immediately.
        m.uc.mem_write(channel + 120, b'\x09')
        received = []
        self.hook(0x1c000240, lambda: received.append((m.reg(0), m.reg(1))) or 0)
        self.call(0xc86eb0c, channel, 1)
        self.assertEqual(received, [])
        self.call(0xc86eb0c, channel, 2)
        self.assertEqual(len(received), 1)
        event, p = received[0]
        self.assertEqual(event, 3)
        self.assertEqual(self.allocations[p], 120)
        self.assertEqual(self.rust('channel', p), 0x42420000 | 1008)
        self.assertEqual(m.uc.mem_read(p + 100, 8), b'ABCDEF\0\0')
        self.rust('free', p)
        self.assertEqual(self.allocations, {})

    def test_compiled_timer_cancel_frees_token_once(self):
        m = self.m
        token = self.rust('buffer', 8, 0)
        self.hook(0xc88e5f0, lambda: 0x87654321)
        self.hook(0xc88e5d4, lambda: 0)
        h = self.rust('timer', 0x200d4000, 0x1c000201, token)
        record = m.word(0x200be884)
        self.assertEqual([m.word(record + n) for n in (0, 4, 8, 12, 16)],
                         [1, 0x200d4000, 0x1c000201, token, h])
        slot = 0x200d4800
        m.word(slot, h)
        self.assertEqual(self.rust('cancel', slot), 0)
        self.assertEqual(m.word(slot), 0)
        self.rust('cancel', slot)
        self.assertEqual(self.events.count(('free', token)), 1)
        self.assertEqual(self.allocations, {})

    def test_compiled_queue_cancel_uses_three_argument_thunk(self):
        self.m.word(0x200be8f4, 0)
        arg = self.rust('buffer', 4, 0)
        self.rust('queue', 0x200d4000, 0x1c000201, arg)
        self.assertEqual(self.events.count(('free', arg)), 1)
        self.assertEqual(self.allocations, {})

    def test_compiled_ipc_envelope_and_real_sendrecv(self):
        m = self.m
        adapter, addr = 0x200d4000, 0x200d4300
        m.word(0x200c3148, adapter)
        m.word(adapter + 48, 17)
        m.uc.mem_write(addr, b'ABCDEF')
        sent = []
        reply = [0]
        self.hook(0xc3f4bf8, lambda: self.event('mutex-lock'))
        self.hook(0xc3f4c14, lambda: self.event('mutex-unlock'))
        def send():
            self.assertEqual((m.reg(0), m.reg(2), m.reg(3)), (17, 708, 0))
            sent.append(bytes(m.uc.mem_read(m.reg(1), 708)))
            self.assertEqual(m.word(adapter + 72), m.reg(1))
            return 708
        def wait():
            self.assertEqual(m.reg(0), adapter + 36)
            m.uc.mem_write(m.word(adapter + 72) + 12, bytes(reply))
            return 0
        self.hook(0xc34d4f6, send)
        self.hook(0xc3f51d4, wait)
        for name, args, command, payload, status in [
            ('pair', (addr,), 94, b'ABCDEF\x01\x00', 0),
            ('remove', (addr,), 95, b'ABCDEF\x01\x00', 1),
            ('pair_state', (addr,), 92, b'ABCDEF\x01\x00', 2),
            ('scan', (3, 1), 37, b'\x03\x01', 0),
            ('get_scan', (), 38, b'', 3),
        ]:
            reply[0] = status
            self.assertEqual(self.rust(name, *args), status)
            request = bytearray(708)
            struct.pack_into('<I', request, 0, command)
            request[60:60 + len(payload)] = payload
            self.assertEqual(sent[-1], request)
            self.assertEqual(m.word(adapter + 72), 0)
        m.word(0x200c3148, 0)
        self.assertEqual(self.rust('pair', addr), 7)

    def test_compiled_h4_hook_guard_and_stock_forwarding(self):
        m = self.m
        slot, hook = 0x200bda80, 0x1c000241
        for previous, accepted in [(0, 0), (0xc123457, 0), (0xc863c99, 1), (hook, 1)]:
            m.word(slot, previous)
            self.assertEqual(self.rust('hook', hook), accepted)
            self.assertEqual(m.word(slot), hook if accepted else previous)
        observed = []
        self.hook(0xc863c98, lambda: observed.append(tuple(m.reg(i) for i in range(3))) or 27)
        self.assertEqual(self.rust('forward', 0x200d4000, 0x200d4200, 9), 27)
        self.assertEqual(observed, [(0x200d4000, 0x200d4200, 9)])

    def test_compiled_native_rows_keep_chinese_fonts(self):
        m = self.m
        ui = NativeUI(m)
        parent = ui.new(0, width=212, height=444)
        title, detail = 0x200d4000, 0x200d4100
        m.uc.mem_write(title, '蓝牙耳机\0'.encode())
        m.uc.mem_write(detail, '已连接\0'.encode())
        row = self.rust('row', parent, title, detail, 1)
        self.assertEqual(m.widgets[row]['width'], 204)
        self.assertFalse(m.widgets[row]['flags'] & 16)
        texts = {p: w for p, w in m.widgets.items() if w.get('text') in ('蓝牙耳机', '已连接')}
        self.assertEqual(len(texts), 2)
        for p, w in texts.items():
            self.assertIn(ui.get(p, 0, 0x5a), (ui.FONT28, ui.FONT24))
        self.assertEqual(self.rust('row_update', row, title, detail), 0)
        self.assertEqual(ui.calls[0xc50ec5c], 1)

    def test_compiled_event_uses_u16_code_and_userdata(self):
        m = self.m
        e = 0x200d4000
        m.word(e + 8, 0xdead0007)
        m.word(e + 12, 0x4321)
        self.assertEqual(self.rust('event', e), 0x43210007)

    def test_compiled_sdp_commit_unregister_ownership(self):
        m = self.m
        owner, state = 0x200d4000, 0x200d4200
        m.word(0x200be894, owner)
        m.word(owner, state)
        m.word(0x200bd924, 0x200d4400)
        self.hook(0xc869f34, lambda: 0)  # persistence/cache notification
        self.hook(0xc86b810, lambda: 0x10001)  # allocator for service handles
        p = self.rust('sdp')
        m.uc.mem_write(0x200d4600, b'\x35\x03\x19\x11\x0a')
        self.assertNotEqual(self.rust('sdp_attr', p, 1, 5, 0x200d4600), 0)
        h = self.rust('sdp_commit', p)
        self.assertEqual(h, 0x10001)
        self.assertEqual(m.word(state + 8), p)
        self.assertEqual(self.rust('sdp_unregister', h), 0)
        self.assertEqual(m.word(state + 8), 0)
        self.assertEqual(self.allocations, {})

    def test_compiled_monotonic_clock_body(self):
        m = self.m
        out = 0x200d4000
        def ticks():
            m.uc.mem_write(m.reg(0), struct.pack('<Q', 170020))
            return 0
        # Model only the hardware tick source, execute real timespec conversion.
        self.hook(0xc8c2644, ticks)
        self.assertEqual(self.rust('clock', 1, out), 0)
        self.assertEqual(struct.unpack('<qI', m.uc.mem_read(out, 12)), (17, 2000000))


if __name__ == '__main__':
    unittest.main(defaultTest='PrivateBackendTests')
