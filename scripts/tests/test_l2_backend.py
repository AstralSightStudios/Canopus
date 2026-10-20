"""Execute the real Cortex-M33 L2 shim against a modeled firmware boundary."""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

try:
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0
except ImportError:
    Uc = None

ROOT = Path(__file__).resolve().parents[2]


class L2ConfigTests(unittest.TestCase):
    def test_default_backend_follows_exact_target(self):
        with tempfile.TemporaryDirectory(prefix="canopus-l2-config-") as tmp:
            header = Path(tmp) / "canopus_l2_probe_config.h"
            for target, available in [
                ("xiaomi-band-10-pro-3.101.043", True),
                ("xiaomi-band-11-4.100.155", True),
                ("xiaomi-band-11-4.100.179", True),
                ("xiaomi-band-10-pro-3.101.036", False),
                ("xiaomi-band-11-4.100.139", False),
            ]:
                with self.subTest(target=target):
                    subprocess.run([sys.executable, str(ROOT / "scripts/generate_l2_probe_config.py"),
                                    target, str(header)], check=True, capture_output=True)
                    config = header.read_text()
                    self.assertIn(f"#define CANOPUS_SUP_HAS_L2 {int(available)}", config)
                    self.assertEqual("L2_PROBE_BT_LOOP" in config, available)


@unittest.skipIf(Uc is None, "unicorn not installed; ARM L2 execution skipped")
class L2BackendTests(unittest.TestCase):
    def test_sram_hook_encrypted_queue_and_session_boundary(self):
        if not shutil.which("clang") or not shutil.which("ld.lld"):
            self.skipTest("ARM compiler/linker not available")
        with tempfile.TemporaryDirectory(prefix="canopus-l2-arm-") as tmp:
            tmp = Path(tmp)
            includes = ["tests/l2", "sdk/c", "manager/service", "manager/protocol", "manager/package"]
            flags = ["--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb", "-Os",
                     "-ffreestanding", "-fno-builtin", "-fno-unwind-tables",
                     "-fno-asynchronous-unwind-tables"]
            flags += ["-I" + str(ROOT / p) for p in includes]
            objects = []
            for source in ["manager/service/canopus_supervisor_l2.c", "tests/l2/harness.c"]:
                obj = tmp / (Path(source).stem + ".o")
                subprocess.run(["clang", *flags, "-c", str(ROOT / source), "-o", str(obj)], check=True)
                objects.append(str(obj))
            elf = tmp / "probe.elf"
            subprocess.run(["ld.lld", "-Ttext=0x10000", "-Tdata=0x20001000", "-e", "exercise",
                            *objects, "-o", str(elf)], check=True)
            data = elf.read_bytes()
            machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
            machine.mem_map(0, 0x40000)
            machine.mem_map(0x20000000, 0x200000)
            phoff = struct.unpack_from("<I", data, 28)[0]
            entsize, count = struct.unpack_from("<HH", data, 42)
            for i in range(count):
                kind, offset, addr, _, size, _, _, _ = struct.unpack_from("<8I", data, phoff + i * entsize)
                if kind == 1 and size:
                    machine.mem_write(addr, data[offset:offset + size])
            machine.reg_write(UC_ARM_REG_SP, 0x201ff000)
            machine.reg_write(UC_ARM_REG_LR, 0x30001)
            entry = struct.unpack_from("<I", data, 24)[0]
            machine.emu_start(entry | 1, 0x30000, count=100000)
            self.assertEqual(machine.reg_read(UC_ARM_REG_R0), 0)


if __name__ == "__main__":
    unittest.main()
