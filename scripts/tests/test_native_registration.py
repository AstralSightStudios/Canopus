"""Execute the real ARM target app/page/launcher registration on three targets.

Firmware lookup, install and launcher calls are modeled, including existing IDs
and installation that resolves to a foreign owner. No firmware image is needed;
this is host/emulator evidence, not a device or allocator concurrency test.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_module_page_back import TARGETS
from test_service_timer_pump import ROOT, SRAM, SRAM_SIZE, STACK, STOP, build_image

EXPORTS = (
    "test_registration_prepare", "test_publish_ui", "test_publish_native",
    "test_registration_step", "test_registration_address", "test_registration_layout",
    "test_registration_status", "test_registration_identity_error",
)
SOURCES = (
    "runtime/ui/canopus_ui.c", "manager/ui/canopus_manager.c",
    "manager/ui/canopus_manager_native.c", "manager/client/canopus_client.c",
    "manager/protocol/canopus_protocol.c", "runtime/module/canopus_module.c",
    "runtime/lifecycle/canopus_lifecycle.c",
)
REGISTRY = 0x20300000
FOREIGN_PACKAGE = 0x20310000


class NativeRegistrationTests(unittest.TestCase):
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
            "native_registration_thumb_elf", ROOT / "tests/hook/test_thumb.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        cls.temp = tempfile.TemporaryDirectory(prefix="canopus-native-registration-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.images = {}
        fixture = ROOT / "tests/host/test_native_registration.c"
        for target in TARGETS:
            out = Path(cls.temp.name) / target
            out.mkdir()
            try:
                cls.images[target] = build_image(
                    out, target, [], cc, linker, helper.load_elf,
                    wrapper_source='#include "' + str(fixture) + '"\n',
                    exports=EXPORTS, sources=SOURCES)
            except subprocess.CalledProcessError as exc:
                raise RuntimeError(exc.stderr) from exc

    def call(self, u, name, *args):
        r = self.regs
        u.reg_write(r.UC_ARM_REG_XPSR, 0x01000000)
        u.reg_write(r.UC_ARM_REG_SP, STACK)
        u.reg_write(r.UC_ARM_REG_LR, STOP | 1)
        for reg, value in zip((r.UC_ARM_REG_R0, r.UC_ARM_REG_R1,
                               r.UC_ARM_REG_R2, r.UC_ARM_REG_R3), (*args, 0, 0, 0, 0)):
            u.reg_write(reg, value)
        u.emu_start(self.symbols[name] | 1, STOP, count=1000000)
        self.assertEqual(u.reg_read(r.UC_ARM_REG_PC), STOP, "Thumb call did not return")
        value = u.reg_read(r.UC_ARM_REG_R0)
        return value if value < 0x80000000 else value - 0x100000000

    @staticmethod
    def word(u, address):
        return struct.unpack("<I", u.mem_read(address, 4))[0]

    @staticmethod
    def half(u, address):
        return struct.unpack("<H", u.mem_read(address, 2))[0]

    @staticmethod
    def string(u, address):
        return bytes(u.mem_read(address, 128)).split(b"\0", 1)[0].decode("utf-8")

    def machine(self, target, occupied=(), foreign_install=False):
        blob, segments, self.symbols = self.images[target]
        u = self.unicorn.Uc(self.unicorn.UC_ARCH_ARM,
                            self.unicorn.UC_MODE_THUMB | self.unicorn.UC_MODE_MCLASS)
        u.mem_map(SRAM, SRAM_SIZE)
        for segment in segments:
            if segment[0] == 1 and segment[4]:
                u.mem_write(segment[2], blob[segment[1]:segment[1] + segment[4]])
        self.call(u, "test_registration_prepare")
        self.layout = [self.call(u, "test_registration_layout", i) for i in range(6)]
        self.installs, self.additions, self.lookups = [], [], []
        self.registry = {}
        u.mem_write(FOREIGN_PACKAGE, b"com.other.native.player\0")
        for app_id in occupied:
            descriptor = REGISTRY + app_id * 128
            raw = bytearray(self.layout[0])
            struct.pack_into("<I", raw, self.layout[1], FOREIGN_PACKAGE)
            struct.pack_into("<H", raw, self.layout[2], app_id)
            u.mem_write(descriptor, bytes(raw))
            self.registry[app_id] = descriptor

        addresses = [self.call(u, "test_registration_address", i) & ~1 for i in range(3)]
        for page in sorted({a & ~0xFFF for a in addresses}):
            u.mem_map(page, 0x1000)

        def native(uc, address, size, data):
            r = self.regs
            first = uc.reg_read(r.UC_ARM_REG_R0)
            result = 0
            if address == addresses[0]:
                self.lookups.append(first)
                result = self.registry.get(first, 0)
            elif address == addresses[1]:
                pointer_array = uc.reg_read(r.UC_ARM_REG_R1)
                count = uc.reg_read(r.UC_ARM_REG_R2)
                app_id = self.half(uc, first + self.layout[2])
                pages = [self.word(uc, pointer_array + 4 * i) for i in range(count)]
                self.installs.append({
                    "id": app_id,
                    "package": self.string(uc, self.word(uc, first + self.layout[1])),
                    "icon": self.string(uc, self.word(uc, first + self.layout[3])),
                    "array": pointer_array,
                    "pages": pages,
                    "page_ids": [self.half(uc, p + self.layout[5]) for p in pages],
                    "page_app_ids": [self.half(uc, p + self.layout[4]) for p in pages],
                })
                # Model firmware retaining a copy, not the caller's descriptor.
                descriptor = REGISTRY + app_id * 128
                raw = bytearray(uc.mem_read(first, self.layout[0]))
                if foreign_install:
                    struct.pack_into("<I", raw, self.layout[1], FOREIGN_PACKAGE)
                uc.mem_write(descriptor, bytes(raw))
                self.registry[app_id] = descriptor
                result = descriptor
            elif address == addresses[2]:
                self.assertIn(first, self.registry)
                self.additions.append(first)
            uc.reg_write(r.UC_ARM_REG_R0, result)
            uc.reg_write(r.UC_ARM_REG_PC, uc.reg_read(r.UC_ARM_REG_LR))

        for address in addresses:
            u.hook_add(self.unicorn.UC_HOOK_CODE, native, begin=address, end=address)
        return u

    def test_public_and_native_use_assigned_identity_in_either_publication_order(self):
        for target in TARGETS:
            for native_first in (False, True):
                with self.subTest(target=target, native_first=native_first):
                    u = self.machine(target)
                    order = ("test_publish_native", "test_publish_ui") if native_first else (
                        "test_publish_ui", "test_publish_native")
                    for publish in order:
                        self.assertEqual(self.call(u, publish), 0)
                    self.assertEqual(self.installs, [])  # publishing only queues
                    for slot in (3, 7):
                        self.assertEqual(self.call(u, "test_registration_status", slot), 1)
                    for _ in range(2):
                        self.assertEqual(self.call(u, "test_registration_step"), 1)
                    # Installation alone must not release the stage-2 gate.
                    for slot in (3, 7):
                        self.assertEqual(self.call(u, "test_registration_status", slot), 1)
                    for _ in range(2):
                        self.assertEqual(self.call(u, "test_registration_step"), 1)
                    for slot in (3, 7):
                        self.assertEqual(self.call(u, "test_registration_status", slot), 2)
                    for _ in range(5):
                        self.assertEqual(self.call(u, "test_registration_step"), 0)
                    expected = [0xCC, 0xD3] if native_first else [0xD3, 0xCC]
                    self.assertEqual([record["id"] for record in self.installs], expected)
                    self.assertEqual(self.additions, expected)
                    records = {record["id"]: record for record in self.installs}
                    ui, raw = records[0xD3], records[0xCC]
                    self.assertEqual(ui["package"], "com.canopus.module.corona")
                    self.assertEqual(raw["package"], "com.canopus.module.native_player")
                    self.assertEqual(ui["icon"], "/data/corona/logo.bin")
                    self.assertEqual(raw["icon"], "/data/player/logo.bin")
                    self.assertEqual(ui["page_ids"], [0])
                    self.assertEqual(ui["page_app_ids"], [0xD3])
                    self.assertEqual(raw["page_ids"], [7, 11])
                    self.assertEqual(raw["page_app_ids"], [0xCC, 0xCC])
                    self.assertEqual(raw["pages"], [
                        self.call(u, "test_registration_address", 3),
                        self.call(u, "test_registration_address", 4)])
                    self.assertNotEqual(raw["array"],
                                        self.call(u, "test_registration_address", 5),
                                        "Queued native pages retained the caller's stack array")

    def test_occupied_id_is_not_installed_or_added_again(self):
        for target in TARGETS:
            with self.subTest(target=target):
                u = self.machine(target, occupied=(0xD3,))
                self.assertEqual(self.call(u, "test_publish_ui"), 0)
                self.assertEqual(self.call(u, "test_publish_native"), 0)
                self.assertLess(self.call(u, "test_registration_step"), 0)
                self.assertEqual(self.call(u, "test_registration_status", 3), 3)
                self.assertEqual(self.call(u, "test_registration_status", 7), 1)
                self.assertEqual(self.installs, [])
                self.assertEqual(self.additions, [])
                self.assertEqual(self.call(u, "test_registration_step"), 1)
                self.assertEqual(self.call(u, "test_registration_status", 7), 1)
                self.assertEqual(self.call(u, "test_registration_step"), 1)
                self.assertEqual(self.call(u, "test_registration_status", 3), 3)
                self.assertEqual(self.call(u, "test_registration_status", 7), 2)
                self.assertEqual(self.call(u, "test_registration_step"), 0)
                self.assertEqual([r["id"] for r in self.installs], [0xCC])
                self.assertEqual(self.additions, [0xCC])

    def test_foreign_owner_after_install_never_gets_a_launcher_entry(self):
        for target in TARGETS:
            with self.subTest(target=target):
                u = self.machine(target, foreign_install=True)
                self.assertEqual(self.call(u, "test_publish_ui"), 0)
                self.assertLess(self.call(u, "test_registration_step"), 0)
                self.assertEqual(self.call(u, "test_registration_status", 3), 3)
                for _ in range(3):
                    self.assertEqual(self.call(u, "test_registration_step"), 0)
                self.assertEqual(len(self.installs), 1)
                self.assertEqual(self.additions, [])


    def test_identity_error_fails_pending_entries_once_then_becomes_idle(self):
        for target in TARGETS:
            for installed_first in (False, True):
                with self.subTest(target=target, installed_first=installed_first):
                    u = self.machine(target)
                    self.assertEqual(self.call(u, "test_publish_ui"), 0)
                    self.assertEqual(self.call(u, "test_publish_native"), 0)
                    if installed_first:
                        self.assertEqual(self.call(u, "test_registration_step"), 1)
                        self.assertEqual(len(self.installs), 1)
                    self.call(u, "test_registration_identity_error", 0xFFFFFF9B)  # -101
                    self.assertEqual(self.call(u, "test_registration_step"), -101)
                    for slot in (3, 7):
                        self.assertEqual(self.call(u, "test_registration_status", slot), 3)
                    for _ in range(4):
                        self.assertEqual(self.call(u, "test_registration_step"), 0)
                    self.assertEqual(len(self.installs), int(installed_first))
                    self.assertEqual(self.additions, [])


if __name__ == "__main__":
    unittest.main()
