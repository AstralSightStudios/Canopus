#!/usr/bin/env python3
"""Audit the stock LuaLVGL path policy using exact .139/.155/.179 Thumb bodies.

Runs no code on a watch. Lua stack operations, VFS stat/open, and allocation
are modeled; path canonicalization, filtering, driver selection, prefix
removal, mode parsing, and POSIX path formatting run from the firmware image.
Run: build/band11-tests/bin/python scripts/tests/band11_no_debug_fs.py -v
"""
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import fwlocate
import hashlib
from pathlib import Path
import struct
import unittest

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R12, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_M33,
)

ROOT = Path(__file__).resolve().parents[2]
IMAGES = {
    "4.100.139": "31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74",
    "4.100.155": "ea0bdf1920cb30223d616432af00565ca67622e6468328f5eab155f8cdc2fb9f",
    "4.100.179": "307adabe0ff61c77be73d045a8260e279cddd131c7a535a2678eb34199278a2b",
}
# .179 moved every address below. Each one is the .155 address translated by its offset
# inside the matching function body (byte-verified, EVID-PORT-4179-001); .139/.155 use the
# numbers in this file as they are (the Lua C API ones shifted by -16 in .155).
MOVED_179 = {
    0x0c33e984: 0x0c33e94c, 0x0c342c54: 0x0c342c1c, 0x0c34cd2c: 0x0c34cc7c,
    0x0c3b0de4: 0x0c3b0d4c, 0x0c349538: 0x0c349488,
    0x0c70b4d8: 0x0c7098b8, 0x0c704532: 0x0c702912, 0x0c70bc58: 0x0c70a038,
    0x0c71191c: 0x0c70fcfc, 0x0c8e8b24: 0x0c8e6f5c, 0x0c704634: 0x0c702a14,
    0x0c70aa7e: 0x0c708e5e, 0x0c70465c: 0x0c702a3c,
    0x0c3a9bc2: 0x0c3a9aee, 0x0c3a9c12: 0x0c3a9b3e, 0x0c3a6194: 0x0c3a60c0,
    0x0c6cfe88: 0x0c6ce274,
    # the undump (binary chunk) audit: the Lua block moved by -0x1c20 as a whole
    0x0c70a224: 0x0c708604, 0x0c70a28a: 0x0c70866a, 0x0c70a304: 0x0c7086e4,
    0x0c709e68: 0x0c708248, 0x0c710c8c: 0x0c70f06c, 0x0c7111b0: 0x0c70f590,
}
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)


class PathMachine:
    PATH, MODE, USERDATA, CACHE = 0x200d4000, 0x200d5000, 0x200d8000, 0x200d9000
    STOP, STACK = 0x200d0000, 0x2015d000

    def __init__(self, version, colon_component_exists=False):
        data = (fwlocate.locate(f"xiaomi-band-11-{version}") or ROOT / "fwbins" / f"xiaomi-band-11-{version}" / "vela_ap.bin").read_bytes()
        assert hashlib.sha256(data).hexdigest() == IMAGES[version]
        self.delta = 0 if version == "4.100.139" else -16
        self.version = version
        self.GLOBAL = 0x200bd08c if version == "4.100.179" else 0x200bd1e8
        self.u = u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        for base in (0x0c0c0000, 0x2c0c0000):
            u.mem_map(base, (len(data) + 4095) & ~4095)
            u.mem_write(base, data)
        u.mem_map(0x20000000, 0x200000)
        u.mem_map(0x00260000, 0x140000)
        for dst, src, size in ((0x2006be00, 0x1b3c, 0xb710),
                               (0x20079420, 0xd24c, 0x2f8c)):
            u.mem_write(dst, data[src:src + size])
            u.mem_write(dst - 0x1fe00000, data[src:src + size])
        self.opens, self.stats, self.lua_results = [], [], []
        self.colon_component_exists = colon_component_exists
        self.hook(self.at(0x0c33e984), self.stat)
        self.hook(self.at(0x0c342c54), self.open)
        self.hook(self.at(0x0c34cd2c), self.free_null)
        self.hook(self.at(0x0c3b0de4), self.allocate_cache)
        self.hook(self.at(0x0c349538), lambda: 0x200d6000)
        # These addresses were independently checked in both raw images.
        for address, fn in {
            0x0c70b4e8: self.lua_string,
            0x0c704542: lambda: 4,  # LUA_TSTRING for the mode argument
            0x0c70bc68: lambda: self.USERDATA,
            0x0c71192c: lambda: 5,  # getfield(registry, "lv_fs") -> table
            0x0c8e8b34: lambda: 1,  # setmetatable
            0x0c704644: lambda: self.result("nil"),
            0x0c70aa8e: lambda: self.result("message"),
            0x0c70466c: lambda: self.result(self.reg(2)),
        }.items():
            self.hook(self.at(address + self.delta), fn)
        # Execute the real POSIX-driver initialization fragment. Stop before
        # insertion into the LVGL linked list, whose single node is modeled.
        u.reg_write(UC_ARM_REG_R4, self.GLOBAL)
        u.reg_write(UC_ARM_REG_R5, self.GLOBAL + 0x1d0)
        u.reg_write(UC_ARM_REG_SP, self.STACK)
        u.emu_start(self.at(0x0c3a9bc2) | 1, self.at(0x0c3a9c12), count=10000)
        assert u.reg_read(UC_ARM_REG_PC) == self.at(0x0c3a9c12)
        assert u.mem_read(self.GLOBAL + 0x1d0, 1) == b"/"
        assert self.word(self.GLOBAL + 0x1d4) == 4096
        assert self.word(self.GLOBAL + 0x1dc) == self.at(0x0c3a6194) | 1
        self.word(self.GLOBAL + 0x1c4, 4)
        self.word(self.GLOBAL + 0x1c8, 0x200d2000)
        self.word(0x200d2000, self.GLOBAL + 0x1d0)

    def at(self, address):
        """The address in this image: identity for .139/.155, the translated one for .179."""
        return MOVED_179[address] if self.version == "4.100.179" else address

    def reg(self, index):
        return self.u.reg_read(REGS[index])

    def word(self, address, value=None):
        if value is not None:
            self.u.mem_write(address, struct.pack("<I", value))
        return struct.unpack("<I", self.u.mem_read(address, 4))[0]

    def string(self, address):
        data = bytearray()
        for i in range(1024):
            value = self.u.mem_read(address + i, 1)[0]
            if value == 0:
                return data.decode()
            data.append(value)
        raise AssertionError("unterminated string")

    def hook(self, address, fn):
        def callback(u, pc, size, _):
            value = fn()
            for reg in (*REGS[1:], UC_ARM_REG_R12):
                u.reg_write(reg, 0xdeadc0de)
            u.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
        self.u.hook_add(UC_HOOK_CODE, callback, None, address, address)

    def stat(self):
        path = self.string(self.reg(0))
        self.stats.append(path)
        if ":" in path and not self.colon_component_exists:
            return -1
        self.word(self.reg(1) + 8, 0x4000)  # directory, no symlink
        return 0

    def open(self):
        self.opens.append((self.string(self.reg(0)), self.reg(1)))
        return 7  # modeled descriptor; never opens a host/device file

    def free_null(self):
        assert self.reg(0) == 0
        return 0

    def allocate_cache(self):
        assert self.reg(0) == 16
        return self.CACHE

    def lua_string(self):
        assert self.reg(1) in (1, 2)
        return self.PATH if self.reg(1) == 1 else self.MODE

    def result(self, value):
        self.lua_results.append(value)
        return 0

    def open_file(self, path, mode="r"):
        self.u.mem_write(self.PATH, path.encode() + b"\0")
        self.u.mem_write(self.MODE, mode.encode() + b"\0")
        self.u.reg_write(UC_ARM_REG_R0, 0x200da000)  # modeled lua_State
        self.u.reg_write(UC_ARM_REG_SP, self.STACK)
        self.u.reg_write(UC_ARM_REG_LR, self.STOP | 1)
        self.u.emu_start(self.at(0x0c6cfe98 + self.delta) | 1, self.STOP, count=100000)
        assert self.u.reg_read(UC_ARM_REG_PC) == self.STOP
        assert self.u.reg_read(UC_ARM_REG_SP) == self.STACK
        return self.reg(0)


class NoDebugPathTests(unittest.TestCase):
    def test_direct_device_paths_are_denied(self):
        for version in IMAGES:
            for path in ("/dev/null", "/dev", "/proc/memdump", "/sys/test"):
                with self.subTest(version=version, path=path):
                    m = PathMachine(version)
                    self.assertEqual(m.open_file(path), 3)
                    self.assertEqual(m.lua_results, ["nil", "message", 6])
                    self.assertEqual(m.opens, [])

    def test_lvgl_colon_prefix_reaches_posix_open(self):
        for version in IMAGES:
            for exists in (False, True):
                with self.subTest(version=version, colon_component_exists=exists):
                    m = PathMachine(version, exists)
                    self.assertEqual(m.open_file("/:dev/null"), 1)
                    self.assertEqual(m.opens, [("/dev/null", 1)])
                    self.assertIn("/:dev", m.stats)
                    self.assertEqual(m.word(m.USERDATA), 8)  # fd + 1

    def test_normal_public_path_still_uses_the_same_driver(self):
        for version in IMAGES:
            with self.subTest(version=version):
                m = PathMachine(version)
                self.assertEqual(m.open_file("/data/canopus/test.txt"), 1)
                self.assertEqual(m.opens, [("/data/canopus/test.txt", 1)])


if __name__ == "__main__":
    unittest.main()
