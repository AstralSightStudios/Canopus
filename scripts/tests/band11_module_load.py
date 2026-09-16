#!/usr/bin/env python3
"""Load a real signed Canopus module through the selected Band 11 Supervisor.

Covers the path that crashes on hardware when it is wrong: RESTORE_AFTER_BOOT
reads the staged artifact, lays the image out in Umem, relocates it against the
PSRAM instruction alias, publishes it through the fixed cache leaves and runs
its constructors there. Firmware allocation/VFS are modeled and physical cache
behaviour is not, so this is not a device result.

A signed receipt cannot be produced without the installer private key, so the
module and its receipt come from an existing build. Point CANOPUS_MODULE_DIR at
a directory holding `receipt.bin` plus the matching `.elf`; the test skips when
it is absent.

    build/band11-tests/bin/python scripts/tests/band11_module_load.py
"""
import os
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from band11_arm_bootstrap import Machine, ROOT, TARGET
from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2

MODULE_TARGET = os.environ.get('CANOPUS_TEST_TARGET', TARGET)
DEFAULT_DIR = ROOT.parent / 'Canopus-Module-BluetoothAudio/build/bluetooth-audio-prod' / MODULE_TARGET
MODULE_DIR = pathlib.Path(os.environ.get('CANOPUS_MODULE_DIR', DEFAULT_DIR))
RESTORE_AFTER_BOOT = 0x4351000A
CMD_MAGIC = 0x43504331
STATUS_MAGIC = 0x43505331
INTENT_ENABLED = 1
SIGNATURE_OK = 1


def staged():
    """Returns (module_id, lifecycle, version, receipt, elf) or None."""
    receipt_path = MODULE_DIR / 'receipt.bin'
    if not receipt_path.is_file():
        return None
    receipt = receipt_path.read_bytes()
    if len(receipt) != 256 or struct.unpack_from('<I', receipt)[0] != 0x31494D43:
        return None
    lifecycle, version, artifact_size = struct.unpack_from('<3I', receipt, 16)
    module_id = receipt[32:64].split(b'\0')[0].decode()
    for elf_path in sorted(MODULE_DIR.glob('*.elf')):
        elf = elf_path.read_bytes()
        if len(elf) == artifact_size:
            return module_id, lifecycle, version, receipt, elf
    return None


def registry(module_id, lifecycle, version):
    buf = bytearray(16 + 16 * 48)
    struct.pack_into('<3I', buf, 0, 0x31524443, 1, 1)  # CRD1, version, count
    name = module_id.encode()
    buf[16:16 + len(name)] = name
    struct.pack_into('<4I', buf, 16 + 32, lifecycle, version, SIGNATURE_OK,
                     INTENT_ENABLED)
    return bytes(buf)


@unittest.skipUnless(staged(), f'no staged module in {MODULE_DIR}')
class ModuleLoadTests(unittest.TestCase):
    def setUp(self):
        module_id, lifecycle, version, receipt, elf = staged()
        self.assertEqual(receipt[64:112].split(b'\0', 1)[0].decode(), MODULE_TARGET,
                         'fixture receipt must belong to the selected firmware')
        self.module_id, self.elf = module_id, elf
        self.m = m = Machine(target=MODULE_TARGET)
        m.disk['/data/canopus/registry.bin'] = registry(module_id, lifecycle, version)
        m.disk[f'/data/canopus/inbox/{module_id}.cmi'] = receipt
        m.disk[f'/data/canopus/inbox/{module_id}.ko'] = elf
        # The module's own constructor uses libc malloc. Model it the way the
        # harness models mm_memalign: the emulator has no scheduler, so the
        # real heap mutex would assert. Its free is the already-hooked
        # umm_free 0x0c34cd2c.
        m.uc.hook_add(UC_HOOK_CODE, m.firmware_call, self.libc_malloc,
                      0x0c351724, 0x0c351724)
        self.alias_blocks = []
        m.uc.hook_add(UC_HOOK_BLOCK, self.watch_alias)

    def libc_malloc(self):
        m = self.m
        size = m.reg(0)
        p = (m.next_temp + 7) & ~7
        m.next_temp = p + size + 16
        m.allocations[p] = size
        m.uc.mem_write(p, bytes(size))
        return p

    def watch_alias(self, uc, address, size, data):
        if 0x1c000000 <= address < 0x1d000000:
            self.alias_blocks.append(address)

    def status(self):
        buf = 0x200d1000
        self.m.uc.reg_write(UC_ARM_REG_R1, buf)
        self.m.uc.reg_write(UC_ARM_REG_R2, 384)
        self.assertEqual(self.m.call(self.m.word(self.m.fops + 8)), 384)
        self.assertEqual(self.m.word(buf), STATUS_MAGIC)
        return buf

    def test_enabled_module_is_restored_into_umem_through_the_alias(self):
        m = self.m
        self.assertEqual(m.boot(), 0)
        mpu_before = m.word(0x200f5190)
        kernel_before = m.peak_kernel
        resident_before = set(m.allocations)
        # ITT-NE inside client_exchange triggers a Unicorn 2.1.4 memory-hook
        # defect; execution/MPU checks and stack canaries stay active.
        m.finish_access_monitor()
        self.alias_blocks.clear()

        frame = 0x200d0000
        m.uc.mem_write(frame, struct.pack('<4I', CMD_MAGIC, RESTORE_AFTER_BOOT, 0, 0))
        m.uc.reg_write(UC_ARM_REG_R1, frame)
        m.uc.reg_write(UC_ARM_REG_R2, 16)
        self.assertEqual(m.call(m.word(m.fops + 12)), 16)

        buf = self.status()
        self.assertEqual(m.word(buf + 20), RESTORE_AFTER_BOOT)
        self.assertEqual(m.word(buf + 16), 1, 'module slot was not imported')
        # The module is resident: its constructor registered a descriptor, so
        # the slot advanced past INSTALLED. Whether its own activate() succeeds
        # depends on firmware services this emulator does not provide.
        self.assertNotEqual(m.word(buf + 128), 0, 'slot is empty')

        live = {p: n for p, n in m.allocations.items()
                if p not in m.frees and p not in resident_before}
        image = [(p, n) for p, n in live.items() if n >= 0x10000]
        self.assertEqual(len(image), 1, f'expected one module image, got {image}')
        address, size = image[0]
        self.assertGreaterEqual(address, 0x3c000000, 'image is not in Umem')
        self.assertLess(address, 0x3d000000, 'image is not in Umem')
        self.assertEqual(size & 31, 0)

        # Kmem is untouched by the image, and no MPU lease was taken: the
        # privileged default Code map already covers the alias.
        self.assertEqual(m.peak_kernel, kernel_before, 'module image consumed Kmem')
        self.assertEqual(m.word(0x200f5190), mpu_before, 'module took an MPU lease')

        # Code really ran through the instruction alias of that allocation.
        alias = address - 0x20000000
        self.assertTrue(any(alias <= b < alias + size for b in self.alias_blocks),
                        'no module code executed through the PSRAM alias')

        # The publish sequence reached both controllers: D clean-all at
        # 0x07ffc034 and, with the I cache already enabled, its
        # clean/invalidate-all command at 0x07ffa038.
        self.assertEqual(m.word(0x07ffc034), 1, 'D-cache clean-all was not issued')
        self.assertEqual(m.word(0x07ffa038), 1, 'I-cache invalidation was not issued')
        self.assertEqual(m.word(0x07ffa000) & 1, 1, 'I-cache was disabled')
        self.assertEqual(m.word(0x07ffc000) & 1, 1, 'D-cache was disabled')

    def test_starved_kernel_heap_does_not_stop_a_umem_module(self):
        # The device reported largest:51056 for Kmem; the module image is far
        # larger. Umem residency has to make that irrelevant.
        m = self.m
        self.assertEqual(m.boot(), 0)
        resident_before = set(m.allocations)
        m.finish_access_monitor()
        m.kernel_largest = 4096
        frame = 0x200d0000
        m.uc.mem_write(frame, struct.pack('<4I', CMD_MAGIC, RESTORE_AFTER_BOOT, 0, 0))
        m.uc.reg_write(UC_ARM_REG_R1, frame)
        m.uc.reg_write(UC_ARM_REG_R2, 16)
        self.assertEqual(m.call(m.word(m.fops + 12)), 16)
        buf = self.status()
        self.assertNotEqual(m.word(buf + 128), 0, 'slot is empty')
        live = {p: n for p, n in m.allocations.items()
                if p not in m.frees and p not in resident_before}
        self.assertTrue(any(p >= 0x3c000000 and n >= 0x10000 for p, n in live.items()),
                        'module image was not served from Umem')
        self.assertFalse(any(p < 0x30000000 for p in live),
                         'the starved Kmem was drawn on at all')


if __name__ == '__main__':
    unittest.main(verbosity=2)
