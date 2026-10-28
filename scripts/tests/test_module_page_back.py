"""Execute real module-page descriptor/back glue for the three reviewed targets.

The ARM target source and portable UI/model/client runtime are real; registry,
heap, rendering and firmware finish are modeled. This is host/emulator evidence.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_service_timer_pump import ROOT, SRAM, SRAM_SIZE, STACK, STOP, build_image

TARGETS = (
    "xiaomi-band-11-4.100.155",
    "xiaomi-band-11-4.100.179",
    "xiaomi-band-10-pro-3.101.043",
)
PAYLOAD = 0x203F1001  # Borrowed payload need not be uint16-aligned.
EXPORTS = (
    "test_prepare", "test_open", "test_callback", "test_other_descriptor",
    "test_destroy", "test_pause", "test_inactive",
    "test_title_back", "test_finish_address", "test_heap_address", "test_heap_size",
    "test_state", "test_finish_destroy",
)


class ModulePageBackTests(unittest.TestCase):
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
            "module_back_thumb_elf", ROOT / "tests/hook/test_thumb.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        cls.temp = tempfile.TemporaryDirectory(prefix="canopus-module-page-back-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.images = {}
        fixture = ROOT / "tests/host/test_module_page_back.c"
        wrapper = '#include "' + str(fixture) + '"\n'
        baseline = os.environ.get("CANOPUS_MANAGER_BACK_SOURCE")
        if baseline:
            wrapper = '#define CANOPUS_MANAGER_TARGET_SOURCE "' + baseline + '"\n' + wrapper
        sources = ("runtime/ui/canopus_ui.c", "manager/ui/canopus_manager.c",
                   "manager/ui/canopus_manager_native.c", "manager/client/canopus_client.c",
                   "manager/protocol/canopus_protocol.c", "runtime/module/canopus_module.c",
                   "runtime/lifecycle/canopus_lifecycle.c")
        for target in TARGETS:
            out = Path(cls.temp.name) / target
            out.mkdir()
            try:
                cls.images[target] = build_image(
                    out, target, [], cc, linker, helper.load_elf,
                    wrapper_source=wrapper, exports=EXPORTS, sources=sources)
            except subprocess.CalledProcessError as exc:
                raise RuntimeError(exc.stderr) from exc

    def call(self, u, name, *args):
        address = self.symbols[name] if isinstance(name, str) else name
        r = self.regs
        u.reg_write(r.UC_ARM_REG_XPSR, 0x01000000)
        u.reg_write(r.UC_ARM_REG_SP, STACK)
        u.reg_write(r.UC_ARM_REG_LR, STOP | 1)
        for reg, value in zip((r.UC_ARM_REG_R0, r.UC_ARM_REG_R1,
                               r.UC_ARM_REG_R2, r.UC_ARM_REG_R3), (*args, 0, 0, 0, 0)):
            u.reg_write(reg, value)
        u.emu_start(address | 1, STOP, count=1000000)
        self.assertEqual(u.reg_read(r.UC_ARM_REG_PC), STOP, "Thumb call did not return")
        self.assertEqual(self.freed_reads, [], "Page storage read after synchronous destruction")
        value = u.reg_read(r.UC_ARM_REG_R0)
        return value if value < 0x80000000 else value - 0x100000000

    def machine(self, target, launcher=0, mode=0, opened=True, synchronous=True):
        blob, segments, self.symbols = self.images[target]
        u = self.unicorn.Uc(self.unicorn.UC_ARCH_ARM,
                            self.unicorn.UC_MODE_THUMB | self.unicorn.UC_MODE_MCLASS)
        u.mem_map(SRAM, SRAM_SIZE)
        for segment in segments:
            if segment[0] == 1 and segment[4]:
                u.mem_write(segment[2], blob[segment[1]:segment[1] + segment[4]])
        self.freed_reads, self.finishes = [], []
        self.descriptor = self.call(u, "test_prepare", launcher, mode)
        self.callback = self.call(u, "test_callback", 0)
        self.phase = 0x4a if target.endswith(".043") else 0x48
        native = self.call(u, "test_finish_address")
        u.mem_map(native & ~0xFFF, 0x1000)
        expected_arg = self.descriptor if target.endswith(".043") else (
            (0xcc << 16) if launcher else (0xca << 16) | 3)

        def finish(uc, address, size, data):
            self.finishes.append(uc.reg_read(self.regs.UC_ARM_REG_R0))
            self.assertEqual(self.finishes[-1], expected_arg)
            if synchronous:
                uc.reg_write(self.regs.UC_ARM_REG_PC, self.symbols["test_finish_destroy"] | 1)
            else:
                uc.reg_write(self.regs.UC_ARM_REG_R0, 0)
                uc.reg_write(self.regs.UC_ARM_REG_PC, uc.reg_read(self.regs.UC_ARM_REG_LR))

        u.hook_add(self.unicorn.UC_HOOK_CODE, finish, begin=native & ~1, end=native & ~1)
        heap = self.call(u, "test_heap_address")
        heap_size = self.call(u, "test_heap_size")

        def read_heap(uc, access, address, size, value, data):
            released = struct.unpack("<I", uc.mem_read(self.symbols["fixture_released"], 4))[0]
            if released:
                self.freed_reads.append(address)

        u.hook_add(self.unicorn.UC_HOOK_MEM_READ, read_heap, begin=heap, end=heap + heap_size - 1)
        if opened:
            self.assertEqual(self.call(u, "test_open"), 0)
        return u

    def state(self, u, index):
        return self.call(u, "test_state", index)

    def swipe(self, u, phase=None, descriptor=None, payload=PAYLOAD):
        self.assertNotEqual(self.callback, 0, "Module descriptor +104 is unset")
        if payload:
            u.mem_write(payload, b"\0" * 8 + struct.pack("<H", self.phase if phase is None else phase))
        return self.call(u, self.callback,
                         self.descriptor if descriptor is None else descriptor, payload)

    def test_descriptor_wiring_and_manager_pages_unchanged(self):
        for target in TARGETS:
            for launcher in (0, 1):
                with self.subTest(target=target, launcher=launcher):
                    u = self.machine(target, launcher, opened=False)
                    self.assertNotEqual(self.callback, 0)
                    self.assertEqual(struct.unpack("<I", u.mem_read(self.descriptor + 104, 4))[0],
                                     self.callback)
                    self.assertEqual(self.call(u, "test_callback", 1), self.callback)
                    self.assertEqual(self.call(u, "test_callback", 5), self.callback)
                    for page in range(3):
                        self.assertEqual(self.call(u, "test_callback", page + 2), 0)

    def test_completed_swipe_pops_internal_route_like_title(self):
        for target in TARGETS:
            for launcher in (0, 1):
                for title in (False, True):
                    with self.subTest(target=target, launcher=launcher, title=title):
                        u = self.machine(target, launcher)
                        if title:
                            self.call(u, "test_title_back")
                        else:
                            self.assertEqual(self.swipe(u), 1)
                        self.assertEqual(self.state(u, 0), 1)
                        self.assertEqual(self.state(u, 3), 2)
                        self.assertEqual(self.state(u, 8), 200)
                        self.assertEqual(self.state(u, 7), 0)  # unfinished transaction aborted
                        self.assertEqual(self.state(u, 6), 1)
                        self.assertEqual(self.state(u, 1), 0)
                        self.assertEqual(self.finishes, [])
                        self.assertEqual(self.state(u, 4), 0)

    def test_declined_back_lets_the_firmware_play_its_own_transition(self):
        # descriptor +104 must answer zero so sub_C69BB50 runs its own
        # finish_page and page-out animation; finishing the page here would
        # consume the gesture and skip it.
        for target in TARGETS:
            for launcher in (0, 1):
                for mode in (1, 4):
                    with self.subTest(target=target, launcher=launcher, mode=mode):
                        u = self.machine(target, launcher, mode)
                        self.assertEqual(self.swipe(u), 0)
                        self.assertEqual(self.state(u, 0), int(mode == 1))
                        self.assertEqual(self.finishes, [])
                        self.assertEqual(self.state(u, 1), 0)
                        self.assertEqual(self.state(u, 5), 1)
                        self.assertEqual(self.swipe(u), 0)  # still declined
                        self.assertEqual(self.finishes, [])
                        self.assertEqual(self.state(u, 4), 0)

    def test_title_back_still_finishes_a_declined_module(self):
        # A title tap has no firmware default behind it, so the host finishes.
        for target in TARGETS:
            for launcher in (0, 1):
                for mode in (1, 4):
                    with self.subTest(target=target, launcher=launcher, mode=mode):
                        u = self.machine(target, launcher, mode)
                        self.call(u, "test_title_back")
                        self.assertEqual(len(self.finishes), 1)
                        self.assertEqual(self.state(u, 0), int(mode == 1))
                        self.assertEqual(self.state(u, 1), 1)
                        self.assertEqual(self.state(u, 5), 0)
                        self.assertEqual(self.state(u, 4), 0)

    def test_self_close_is_consumed_with_no_second_finish(self):
        for target in TARGETS:
            for launcher in (0, 1):
                for mode in (2, 3):
                    for synchronous in (False, True):
                        with self.subTest(target=target, launcher=launcher,
                                          mode=mode, synchronous=synchronous):
                            u = self.machine(target, launcher, mode, synchronous=synchronous)
                            self.assertEqual(self.swipe(u), 1)
                            self.assertEqual(len(self.finishes), 1)
                            self.assertEqual(self.state(u, 0), 1)
                            if not synchronous:
                                self.assertEqual(self.state(u, 6), 0)
                                self.assertEqual(self.state(u, 1), 0)
                                self.call(u, "test_destroy")
                            self.assertEqual(self.state(u, 1), 1)
                            # The UI context, plus the page record that a launcher
                            # entry owns (the Manager module page uses the
                            # target's scratch record instead of the heap).
                            self.assertEqual(self.state(u, 2), 2 if launcher else 1)
                            self.assertEqual(self.state(u, 5), 0)
                            self.assertEqual(self.state(u, 4), 0)

    def test_noncompleted_phase_does_not_navigate(self):
        for target in TARGETS:
            for launcher in (0, 1):
                with self.subTest(target=target, launcher=launcher):
                    u = self.machine(target, launcher)
                    for phase in (0, self.phase - 1, self.phase + 0x100,
                                  0x48 if self.phase == 0x4a else 0x4a):
                        self.assertEqual(self.swipe(u, phase), 0)
                    self.assertEqual(self.state(u, 0), 0)
                    self.assertEqual(self.state(u, 6), 1)
                    self.assertEqual(self.finishes, [])

    def test_foreign_inactive_paused_and_stale_pages_are_ignored(self):
        for target in TARGETS:
            for launcher in (0, 1):
                for kind in ("foreign", "null-descriptor", "null-payload", "inactive", "paused", "unopened", "destroyed"):
                    with self.subTest(target=target, launcher=launcher, kind=kind):
                        u = self.machine(target, launcher, opened=kind != "unopened")
                        descriptor, payload = None, PAYLOAD
                        if kind == "foreign":
                            descriptor = self.call(u, "test_other_descriptor")
                        elif kind == "null-descriptor":
                            descriptor = 0
                        elif kind == "null-payload":
                            payload = 0
                        elif kind == "inactive":
                            self.call(u, "test_inactive")
                        elif kind == "paused":
                            self.call(u, "test_pause")
                        elif kind == "destroyed":
                            self.call(u, "test_destroy")
                        self.assertEqual(self.swipe(u, descriptor=descriptor, payload=payload), 0)
                        self.assertEqual(self.state(u, 0), 0)
                        self.assertEqual(self.finishes, [])
                        self.assertEqual(self.state(u, 4), 0)


if __name__ == "__main__":
    unittest.main()
