"""Execute the real firmware directory adapter against modeled NuttX entries.

The whole platform translation unit is compiled with each native Manager target's
committed headers. Only the firmware readdir call is modeled in Unicorn; this
checks decoding and service results, not an actual filesystem or physical device.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from test_service_timer_pump import ROOT, SRAM, SRAM_SIZE, STACK, STOP, build_image

TARGETS = (
    "xiaomi-band-11-4.100.155",
    "xiaomi-band-11-4.100.179",
    "xiaomi-band-10-pro-3.101.043",
)
RAW_ENTRY, OUTPUT, DIRECTORY = 0x203F1000, 0x203F2000, 0x203F3000
WRAPPER = r'''
#include "canopus_supervisor_platform.c"
int test_dir_read(void *dir, struct canopus_fs_dirent *entry) {
    return sup_dir_read(0, dir, entry);
}
uint32_t test_native_readdir(void) { return CANOPUS_SUP_NUTTX_READDIR; }
uint32_t test_dirent_size(void) { return sizeof(struct canopus_fs_dirent); }
'''


class ServiceDirectoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import unicorn
            from unicorn import arm_const
        except ImportError as exc:
            raise unittest.SkipTest("Python unicorn is required") from exc
        cc = os.environ.get("CLANG") or shutil.which("clang")
        linker = os.environ.get("LD_LLD") or shutil.which("ld.lld")
        if not cc or not linker:
            raise unittest.SkipTest("ARM clang and ld.lld are required")
        cls.unicorn, cls.regs = unicorn, arm_const
        spec = importlib.util.spec_from_file_location(
            "directory_thumb_elf", ROOT / "tests/hook/test_thumb.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        cls.temp = tempfile.TemporaryDirectory(prefix="canopus-service-directories-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.images = {}
        for target in TARGETS:
            out = Path(cls.temp.name) / target
            out.mkdir()
            cls.images[target] = build_image(
                out, target, [], cc, linker, helper.load_elf,
                wrapper_source=WRAPPER, exports=(
                    "test_dir_read", "test_native_readdir", "test_dirent_size"))

    def call(self, u, name, *args):
        r = self.regs
        u.reg_write(r.UC_ARM_REG_XPSR, 0x01000000)
        u.reg_write(r.UC_ARM_REG_SP, STACK)
        u.reg_write(r.UC_ARM_REG_LR, STOP | 1)
        for reg, value in zip((r.UC_ARM_REG_R0, r.UC_ARM_REG_R1,
                               r.UC_ARM_REG_R2, r.UC_ARM_REG_R3), (*args, 0, 0, 0, 0)):
            u.reg_write(reg, value)
        u.emu_start(self.symbols[name] | 1, STOP, count=100000)
        self.assertEqual(u.reg_read(r.UC_ARM_REG_PC), STOP, "Thumb call did not return")
        value = u.reg_read(r.UC_ARM_REG_R0)
        return value if value < 0x80000000 else value - 0x100000000

    def machine(self, target, entry):
        blob, segments, self.symbols = self.images[target]
        u = self.unicorn.Uc(self.unicorn.UC_ARCH_ARM,
                            self.unicorn.UC_MODE_THUMB | self.unicorn.UC_MODE_MCLASS)
        u.mem_map(SRAM, SRAM_SIZE)
        for segment in segments:
            if segment[0] == 1 and segment[4]:
                u.mem_write(segment[2], blob[segment[1]:segment[1] + segment[4]])
        native = self.call(u, "test_native_readdir")
        u.mem_map(native & ~0xFFF, 0x1000)
        u.mem_write(OUTPUT, b"\xA5" * self.call(u, "test_dirent_size"))
        if entry is not None:
            # Exact-target evidence: d_type at +0, d_name at +1; raw entry is 98 bytes.
            u.mem_write(RAW_ENTRY, entry + b"\0" * (98 - len(entry)))

        def read(uc, address, size, data):
            self.assertEqual(uc.reg_read(self.regs.UC_ARM_REG_R0), DIRECTORY)
            uc.reg_write(self.regs.UC_ARM_REG_R0, RAW_ENTRY if entry is not None else 0)
            uc.reg_write(self.regs.UC_ARM_REG_PC, uc.reg_read(self.regs.UC_ARM_REG_LR))

        u.hook_add(self.unicorn.UC_HOOK_CODE, read, begin=native & ~1, end=native & ~1)
        return u

    def test_raw_entry_names_and_types(self):
        for target in TARGETS:
            for name, kind in ((".", 2), ("..", 2), ("corona.json", 1),
                               ("themes", 2), ("a" * 63, 1)):
                with self.subTest(target=target, name=name):
                    u = self.machine(target, bytes([kind]) + name.encode() + b"\0")
                    self.assertEqual(self.call(u, "test_dir_read", DIRECTORY, OUTPUT), 0)
                    result = bytes(u.mem_read(OUTPUT, self.call(u, "test_dirent_size")))
                    self.assertEqual(result[:64].split(b"\0", 1)[0], name.encode())
                    self.assertEqual(result[64], int(kind == 2))

    def test_overlong_name_is_rejected_without_truncation(self):
        for target in TARGETS:
            with self.subTest(target=target):
                u = self.machine(target, b"\x01" + b"a" * 64 + b"\0")
                self.assertEqual(self.call(u, "test_dir_read", DIRECTORY, OUTPUT), -22)
                self.assertEqual(bytes(u.mem_read(OUTPUT, 64)), b"\0" * 64)

    def test_eof_is_not_an_empty_entry(self):
        for target in TARGETS:
            with self.subTest(target=target):
                u = self.machine(target, None)
                self.assertEqual(self.call(u, "test_dir_read", DIRECTORY, OUTPUT), 1)
                self.assertEqual(bytes(u.mem_read(OUTPUT, 64)), b"\xA5" * 64)


if __name__ == "__main__":
    unittest.main()
