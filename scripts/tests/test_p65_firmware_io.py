"""Execute exact P65 Thumb write/pwrite bodies; never access a device or disk.

Only fd lookup/release, errno and the downstream driver are modeled. The old
binding's seek logic executes from the AP, including its extra stack argument.
Run with Unicorn 2.1.4 and CANOPUS_P65_FIRMWARE pointing to the exact AP/OTA.
This test cannot establish filesystem durability or the cause of a boot loop.
"""
import hashlib
import os
from pathlib import Path
import re
import struct
import unittest
import zipfile

try:
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (
        UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
        UC_CPU_ARM_CORTEX_M33,
    )
except ImportError:
    Uc = None

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = Path(os.environ.get("CANOPUS_P65_FIRMWARE",
                              str(Path.home() / "develop/temp/p65_v3.100.043_vela_ap.bin")))
SHA = "a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858"
REJECTED_PWRITE = 0x0C248455
WRITE = 0x0C2497F1


def firmware_bytes():
    if zipfile.is_zipfile(FIRMWARE):
        with zipfile.ZipFile(FIRMWARE) as archive:
            entries = [i for i in archive.infolist()
                       if Path(i.filename).name == "vela_ap.bin"]
            assert len(entries) == 1 and entries[0].file_size == 11793540
            data = archive.read(entries[0])
    else:
        data = FIRMWARE.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA, "exact P65 AP required"
    return data


class WriteMachine:
    FILE, INODE, FOPS, DRIVER = 0x3C400000, 0x3C400100, 0x3C400200, 0x3C401000
    BUFFER, ERRNO, STOP, STACK = 0x20001000, 0x20003000, 0x20004000, 0x2003F000

    def __init__(self, payload=b"registry", limit=None, fd_error=0, driver_error=0):
        data = firmware_bytes()
        self.u = u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        u.mem_map(0x0C0C0000, (len(data) + 4095) & ~4095)
        u.mem_write(0x0C0C0000, data)
        u.mem_map(0x3C400000, 0x10000)
        u.mem_map(0x20000000, 0x40000)
        u.mem_write(self.BUFFER, payload)
        self.word(self.FILE, 2)  # Exact NuttX write permission bit.
        self.word(self.FILE + 16, self.INODE)
        self.word(self.INODE + 16, self.FOPS)
        self.word(self.FOPS + 12, self.DRIVER | 1)
        # No driver seek callback: real file_seek falls back to f_pos +8/+12.
        self.payload, self.limit = payload, limit
        self.fd_error, self.driver_error = fd_error, driver_error
        self.writes, self.gets, self.puts = [], 0, 0
        self.args = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
        self.saved = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
                      UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
        self.hook(0x0C2444C4, self.get_file)
        self.hook(0x0C244560, self.put_file)
        self.hook(0x0C25823C, lambda: self.ERRNO)
        self.hook(self.DRIVER, self.write_driver)

    def word(self, address, value=None):
        if value is not None:
            self.u.mem_write(address, struct.pack("<I", value & 0xffffffff))
        return struct.unpack("<I", self.u.mem_read(address, 4))[0]

    def position(self, value=None):
        if value is not None:
            self.u.mem_write(self.FILE + 8, struct.pack("<Q", value))
        return struct.unpack("<Q", self.u.mem_read(self.FILE + 8, 8))[0]

    def reg(self, index):
        return self.u.reg_read(self.args[index])

    def hook(self, address, callback):
        def run(u, pc, size, user):
            value = callback()
            # AAPCS caller-saved clobbering exposes wrong assumed prototypes.
            for r in (*self.args[1:], UC_ARM_REG_R12):
                u.reg_write(r, 0xDEADC0DE)
            u.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
        self.u.hook_add(UC_HOOK_CODE, run, None, address, address)

    def get_file(self):
        assert self.reg(0) == 7
        self.gets += 1
        if self.fd_error:
            return self.fd_error
        self.word(self.reg(1), self.FILE)
        return 0

    def put_file(self):
        assert self.reg(0) == self.FILE
        self.puts += 1
        return 0

    def write_driver(self):
        assert self.reg(0) == self.FILE
        if self.driver_error:
            return self.driver_error
        count = self.reg(2)
        if self.limit is not None:
            count = min(count, self.limit)
        data = bytes(self.u.mem_read(self.reg(1), count))
        self.writes.append((self.position(), data))
        self.position(self.position() + count)
        return count

    def call(self, address, offset=0, count=None, stack_poison=0x0123456700001000):
        count = len(self.payload) - offset if count is None else count
        for r, value in zip(self.args, (7, self.BUFFER + offset, count, 0xAABBCCDD)):
            self.u.reg_write(r, value)
        for i, r in enumerate(self.saved):
            self.u.reg_write(r, 0x34560000 + i)
        self.u.reg_write(UC_ARM_REG_SP, self.STACK)
        self.u.reg_write(UC_ARM_REG_LR, self.STOP | 1)
        self.u.mem_write(self.STACK, struct.pack("<Q", stack_poison))
        self.u.emu_start(address, self.STOP, count=10000)
        assert self.u.reg_read(UC_ARM_REG_PC) == self.STOP
        assert self.u.reg_read(UC_ARM_REG_SP) == self.STACK
        for i, r in enumerate(self.saved):
            assert self.u.reg_read(r) == 0x34560000 + i
        result = self.reg(0)
        return result - 0x100000000 if result >= 0x80000000 else result


@unittest.skipUnless(Uc and FIRMWARE.is_file(), "exact P65 AP and Unicorn required")
class P65FirmwareWriteTests(unittest.TestCase):
    def test_header_binds_plain_write_not_pwrite(self):
        header = (ROOT / "manager/target/p65/canopus_p65_abi.h").read_text()
        value = re.search(r"#define CANOPUS_SUP_NUTTX_WRITE UINT32_C\((0x[0-9A-Fa-f]+)\)", header)
        self.assertIsNotNone(value)
        self.assertEqual(int(value[1], 16), WRITE)
        self.assertNotEqual(WRITE, REJECTED_PWRITE)

    def test_old_binding_reads_unsupplied_64_bit_stack_offset(self):
        machine = WriteMachine()
        poison = 0x0123456700001000
        self.assertEqual(machine.call(REJECTED_PWRITE, stack_poison=poison), 8)
        self.assertEqual(machine.writes, [(poison, b"registry")])
        self.assertEqual(machine.position(), 0)  # pwrite restores the old cursor.
        self.assertEqual((machine.gets, machine.puts), (1, 1))

    def test_old_binding_can_fail_before_write_due_to_stack_offset(self):
        machine = WriteMachine()
        self.assertEqual(machine.call(REJECTED_PWRITE, stack_poison=0xfffffffffffffff0), -1)
        self.assertEqual(machine.word(machine.ERRNO), 22)
        self.assertEqual(machine.writes, [])
        self.assertEqual(machine.position(), 0)
        self.assertEqual((machine.gets, machine.puts), (1, 1))

    def test_plain_write_is_sequential_and_ignores_stack_poison(self):
        machine = WriteMachine(limit=3)
        used = 0
        while used < len(machine.payload):
            count = machine.call(WRITE, offset=used)
            self.assertGreater(count, 0)
            used += count
        self.assertEqual(machine.writes, [(0, b"reg"), (3, b"ist"), (6, b"ry")])
        self.assertEqual(machine.position(), 8)
        self.assertEqual((machine.gets, machine.puts), (3, 3))

    def test_fd_and_driver_errors_report_errno_and_balance_refs(self):
        for get_error, driver_error, expected_errno, expected_puts in (
                (-9, 0, 9, 0), (0, -5, 5, 1), (0, -4, 4, 1)):
            machine = WriteMachine(fd_error=get_error, driver_error=driver_error)
            self.assertEqual(machine.call(WRITE), -1)
            self.assertEqual(machine.word(machine.ERRNO), expected_errno)
            self.assertEqual(machine.writes, [])
            self.assertEqual((machine.gets, machine.puts), (1, expected_puts))

    def test_write_permission_failure_never_calls_driver(self):
        machine = WriteMachine()
        machine.word(machine.FILE, 1)
        self.assertEqual(machine.call(WRITE), -1)
        self.assertEqual(machine.word(machine.ERRNO), 13)
        self.assertEqual(machine.writes, [])
        self.assertEqual((machine.gets, machine.puts), (1, 1))


if __name__ == "__main__":
    unittest.main()
