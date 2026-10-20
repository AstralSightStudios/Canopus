"""Execute the compiled device timer pump with modeled LVGL in Unicorn.

The wrapper includes the real platform translation unit and each target's
committed generated headers. Firmware is not required; LVGL allocation and its
callback event are modeled, so this does not verify UI ownership or a device.
Missing ARM tools or Python unicorn explicitly skip these tests.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]
TARGETS = (
    "xiaomi-band-10-pro-3.101.036",
    "xiaomi-band-10-pro-3.101.043",
    "xiaomi-band-11-4.100.139",
    "xiaomi-band-11-4.100.155",
    "xiaomi-band-11-4.100.179",
)
SRAM, SRAM_SIZE = 0x20000000, 0x400000
STACK, STOP, TIMER = 0x203F0000, 0x203FF000, 0x203F1000

WRAPPER = r'''
#include "canopus_target_config.h"
#ifdef CANOPUS_SUP_BAND11_BOOTSTRAP
#include "band11/canopus_band11_abi.h"
#endif
#ifdef TEST_NO_CLOCK
#undef CANOPUS_SUP_CLOCK_GETTIME
#endif
#ifdef TEST_NO_CREATE
#undef CANOPUS_SUP_LV_TIMER_CREATE
#endif
#include "canopus_supervisor_platform.c"

volatile uint32_t fixture_seen, fixture_data;
void fixture_poll_a(void *data) {
    fixture_seen += 1u;
    fixture_data = (uint32_t)(uintptr_t)data;
}
void fixture_poll_b(void *data) {
    fixture_seen += 100u;
    fixture_data = (uint32_t)(uintptr_t)data;
}
uint32_t test_pump_available(void) { return SUP_TIMER_PUMP != 0; }
uint32_t test_create_address(void) {
#ifdef CANOPUS_SUP_LV_TIMER_CREATE
    return CANOPUS_SUP_LV_TIMER_CREATE;
#else
    return 0u;
#endif
}
int test_pump_start(void (*poll)(void *), void *data) {
    int (*start)(void *, void (*)(void *), void *) = SUP_TIMER_PUMP;
    return start != 0 ? start(0, poll, data) : CANOPUS_SVC_ERR_UNSUPPORTED;
}
'''


def build_image(out, target, defines, cc, linker, load_elf):
    pack = ROOT / "targets" / target
    metadata = tomllib.loads((pack / "target.toml").read_text())
    family = tomllib.loads((ROOT / "targets/_families" /
                           (metadata["family"] + ".toml")).read_text())
    wrapper = out / "pump.c"
    wrapper.write_text(WRAPPER)
    # The Supervisor build generates the target's L2 configuration next to the object.
    subprocess.run(["python3", str(ROOT / "scripts/generate_l2_probe_config.py"), target,
                    str(out / "canopus_l2_probe_config.h")],
                   check=True, capture_output=True, text=True)
    obj, elf = out / "pump.o", out / "pump.elf"
    includes = (
        "sdk/c", "runtime/loader", "manager/service", "manager/target",
        "manager/ui", "manager/native-app", "manager/client", "manager/package",
        "manager/protocol", "third_party/sha256",
    )
    command = [cc, "--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb",
               "-mfloat-abi=soft", "-std=c11", "-ffreestanding", "-fno-builtin",
               "-fno-common", "-fno-stack-protector", "-fno-unwind-tables",
               "-fno-asynchronous-unwind-tables", "-ffunction-sections",
               "-fdata-sections", "-Os", "-Wall", "-Wextra", "-Werror",
               *family["defines"], *defines, "-I" + str(out), "-I" + str(pack / "generated"),
               *["-I" + str(ROOT / path) for path in includes],
               "-c", str(wrapper), "-o", str(obj)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    exports = ("test_pump_start", "test_pump_available", "test_create_address",
               "fixture_poll_a", "fixture_poll_b", "fixture_seen", "fixture_data")
    subprocess.run([linker, "--image-base=0x20000000", "-Ttext=0x20010000",
                    "-Tbss=0x20020000", "--gc-sections", "--entry=test_pump_start",
                    *["--undefined=" + name for name in exports],
                    str(obj), "-o", str(elf)],
                   check=True, capture_output=True, text=True)
    return load_elf(elf)


class ServiceTimerPumpTests(unittest.TestCase):
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
            "timer_thumb_elf", ROOT / "tests/hook/test_thumb.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        cls.temp = tempfile.TemporaryDirectory(prefix="canopus-timer-pump-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.images = {}
        for target in TARGETS:
            for mode, defines in (("ui", []), ("headless", ["-DCANOPUS_SUP_HEADLESS=1"])):
                out = Path(cls.temp.name) / (target + "-" + mode)
                out.mkdir()
                cls.images[target, mode] = build_image(
                    out, target, defines, cc, linker, helper.load_elf)
        for mode, define in (("no-clock", "-DTEST_NO_CLOCK=1"),
                             ("no-create", "-DTEST_NO_CREATE=1")):
            out = Path(cls.temp.name) / mode
            out.mkdir()
            cls.images[TARGETS[3], mode] = build_image(
                out, TARGETS[3], [define], cc, linker, helper.load_elf)

    def machine(self, target, mode="ui"):
        blob, segments, self.symbols = self.images[target, mode]
        u = self.unicorn.Uc(self.unicorn.UC_ARCH_ARM,
                            self.unicorn.UC_MODE_THUMB | self.unicorn.UC_MODE_MCLASS)
        u.mem_map(SRAM, SRAM_SIZE)
        for segment in segments:
            if segment[0] == 1 and segment[4]:
                u.mem_write(segment[2], blob[segment[1]:segment[1] + segment[4]])
        return u

    def call(self, u, name, *args):
        address = self.symbols[name] if isinstance(name, str) else name
        r = self.regs
        u.reg_write(r.UC_ARM_REG_XPSR, 0x01000000)
        u.reg_write(r.UC_ARM_REG_SP, STACK)
        u.reg_write(r.UC_ARM_REG_LR, STOP | 1)
        for reg, value in zip((r.UC_ARM_REG_R0, r.UC_ARM_REG_R1,
                               r.UC_ARM_REG_R2, r.UC_ARM_REG_R3), (*args, 0, 0, 0, 0)):
            u.reg_write(reg, value)
        u.emu_start(address | 1, STOP, count=100000)
        self.assertEqual(u.reg_read(r.UC_ARM_REG_PC), STOP, "Thumb call did not return")
        value = u.reg_read(r.UC_ARM_REG_R0)
        return value if value < 0x80000000 else value - 0x100000000

    def word(self, u, name):
        return struct.unpack("<I", u.mem_read(self.symbols[name], 4))[0]

    def test_exact_target_creation_retry_callback_and_binding(self):
        for target in TARGETS:
            with self.subTest(target=target):
                u = self.machine(target)
                self.assertEqual(self.call(u, "test_pump_available"), 1)
                records = [json.loads(path.read_text()) for path in
                           (ROOT / "targets" / target / "symbols").glob("*.json")]
                record, = [record for record in records if record["name"] == "lv_timer_create"]
                native = int(record["callable_address"], 16)
                self.assertEqual(self.call(u, "test_create_address"), native)
                u.mem_map(native & ~0xFFF, 0x1000)
                calls = []
                result = [0]  # First allocation fails; the next start must retry.

                def create(uc, address, size, data):
                    r = self.regs
                    calls.append(tuple(uc.reg_read(reg) for reg in
                                       (r.UC_ARM_REG_R0, r.UC_ARM_REG_R1, r.UC_ARM_REG_R2)))
                    uc.reg_write(r.UC_ARM_REG_R0, result[0])
                    uc.reg_write(r.UC_ARM_REG_PC, uc.reg_read(r.UC_ARM_REG_LR))

                u.hook_add(self.unicorn.UC_HOOK_CODE, create,
                           begin=native & ~1, end=native & ~1)
                a, b = self.symbols["fixture_poll_a"], self.symbols["fixture_poll_b"]
                self.assertEqual(self.call(u, "test_pump_start", a, 0x1234), -12)
                self.assertEqual(len(calls), 1)
                result[0] = TIMER
                # A failed start must not pin the old callback/data binding.
                self.assertEqual(self.call(u, "test_pump_start", b, 0x5678), 0)
                self.assertEqual(len(calls), 2)
                self.assertEqual(calls[0][1:], (10, 0))
                self.assertEqual(calls[1][1:], (10, 0))
                self.assertEqual(self.call(u, "test_pump_start", b, 0x5678), 0)
                self.assertEqual(self.call(u, "test_pump_start", a, 0x5678), -16)
                self.assertEqual(self.call(u, "test_pump_start", b, 0x1234), -16)
                self.assertEqual(len(calls), 2, "Repeated/conflicting start allocated another timer")
                self.call(u, calls[-1][0], TIMER)
                self.assertEqual(self.word(u, "fixture_seen"), 100)
                self.assertEqual(self.word(u, "fixture_data"), 0x5678)
                self.call(u, calls[-1][0], TIMER)
                self.assertEqual(self.word(u, "fixture_seen"), 200)

    def test_headless_has_no_pump_on_any_target(self):
        for target in TARGETS:
            with self.subTest(target=target):
                u = self.machine(target, "headless")
                self.assertEqual(self.call(u, "test_pump_available"), 0)
                self.assertEqual(self.call(u, "test_pump_start", 0, 0), -38)

    def test_missing_clock_or_timer_entry_has_no_pump(self):
        for mode in ("no-clock", "no-create"):
            with self.subTest(mode=mode):
                u = self.machine(TARGETS[3], mode)
                self.assertEqual(self.call(u, "test_pump_available"), 0)
                self.assertEqual(self.call(u, "test_pump_start", 0, 0), -38)


if __name__ == "__main__":
    unittest.main()
