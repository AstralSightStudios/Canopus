#!/usr/bin/env python3
"""Load-only audit of the exact firmware Lua binary parser; never runs chunks.

The real f_parser/undump functions run in Unicorn. Lua allocation, closure/Proto
creation and stack reservation are modeled. A successful parse does NOT prove
native execution or a usable memory primitive. The PathMachine fixture supplies
the image mappings; none of its filesystem/LuaLVGL hooks is on this call path.
"""
import struct
import unittest

from band11_no_debug_fs import IMAGES, PathMachine
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

# Upstream Lua 5.4.0: string.dump(function() return 42 end, true).
# 32/64-bit pointers are not serialized. This target has 64-bit Lua integers
# and doubles, matching this chunk header (verified by the real parser).
VALID_CHUNK = bytes.fromhex(
    "1b4c7561540019930d0a1a0a04080878560000000000000000000000287740"
    "008081810000028301801480480002004700010080808080808080"
)


class ChunkRejected(Exception):
    pass


class BinaryParserMachine(PathMachine):
    LUA, CLOSURE, PROTO = 0x200da000, 0x200db000, 0x200dc000
    PARSER, ZIO, INPUT = 0x200dd000, 0x200dd100, 0x200de000

    def __init__(self, version):
        super().__init__(version)
        self.next_alloc = 0x200e1000
        for address, fn in {
            0x0c70a234: self.allocate,
            0x0c70a29a: self.new_closure,
            0x0c70a314: lambda: self.PROTO,
            0x0c709e78: self.increment_top,
            0x0c710c9c: self.reject,
        }.items():
            self.hook(address + self.delta, fn)

    def allocate(self):
        size = self.reg(1)
        if size == 0:
            return 0
        assert size < 4096
        ptr = self.next_alloc
        self.next_alloc += (size + 15) & ~15
        self.u.mem_write(ptr, b"\0" * size)
        return ptr

    def new_closure(self):
        assert self.reg(1) == 0  # this fixture has no upvalues
        return self.CLOSURE

    def increment_top(self):
        self.word(self.LUA + 0xc, self.word(self.LUA + 0xc) + 16)
        return 0

    def reject(self):
        raise ChunkRejected(self.string(self.reg(1)))

    def parse(self, chunk):
        self.u.mem_write(self.INPUT, chunk)
        self.word(self.ZIO, len(chunk))
        self.word(self.ZIO + 4, self.INPUT)
        self.word(self.PARSER, self.ZIO)
        self.word(self.PARSER + 0x34, 0x200dd200)
        self.word(self.PARSER + 0x38, 0x200dd210)
        self.u.mem_write(0x200dd200, b"b\0")
        self.u.mem_write(0x200dd210, b"@load-only-audit\0")
        self.word(self.LUA + 0xc, 0x200df000)
        self.u.reg_write(UC_ARM_REG_R0, self.LUA)
        self.u.reg_write(UC_ARM_REG_R1, self.PARSER)
        self.u.reg_write(UC_ARM_REG_SP, self.STACK)
        self.u.reg_write(UC_ARM_REG_LR, self.STOP | 1)
        self.u.emu_start((0x0c7111c0 + self.delta) | 1, self.STOP, count=100000)
        assert self.u.reg_read(UC_ARM_REG_PC) == self.STOP
        assert self.word(self.ZIO) == 0
        assert self.word(self.CLOSURE + 12) == self.PROTO
        return bytes(self.u.mem_read(self.word(self.PROTO + 0x34), self.word(self.PROTO + 0x14) * 4))


class BinaryChunkTests(unittest.TestCase):
    def test_valid_chunk_loads_without_execution(self):
        for version in IMAGES:
            with self.subTest(version=version):
                m = BinaryParserMachine(version)
                self.assertEqual(m.parse(VALID_CHUNK), VALID_CHUNK[39:51])

    def test_invalid_opcode_is_copied_without_validation(self):
        for version in IMAGES:
            with self.subTest(version=version):
                chunk = bytearray(VALID_CHUNK)
                struct.pack_into("<I", chunk, 39, 0x7f)  # no such Lua 5.4 opcode
                m = BinaryParserMachine(version)
                self.assertEqual(m.parse(bytes(chunk))[:4], b"\x7f\0\0\0")

    def test_zero_maxstack_is_accepted_without_execution(self):
        for version in IMAGES:
            with self.subTest(version=version):
                chunk = bytearray(VALID_CHUNK)
                self.assertEqual(chunk[37], 2)
                chunk[37] = 0  # existing instructions still address register 0
                m = BinaryParserMachine(version)
                m.parse(bytes(chunk))
                self.assertEqual(m.u.mem_read(m.PROTO + 8, 1), b"\0")

    def test_header_version_mismatch_is_rejected(self):
        for version in IMAGES:
            with self.subTest(version=version):
                chunk = bytearray(VALID_CHUNK)
                chunk[4] = 0x53
                with self.assertRaisesRegex(ChunkRejected, "version mismatch"):
                    BinaryParserMachine(version).parse(bytes(chunk))


if __name__ == "__main__":
    unittest.main()
