"""Exact-firmware send/SAR ownership tests; crypto, allocation and wake are modeled.

Images are optional and SHA-bound to each target's l2-sram-probe evidence. This
executes native transport and SAR bodies, not physical Bluetooth or AES.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fwlocate

try:
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                                   UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                                   UC_ARM_REG_R3)
except ImportError:
    Uc = None

ROOT = Path(__file__).resolve().parents[2]
# Independently read from each SHA-matched image's send and SAR call sites.
PROFILES = {
    "xiaomi-band-10-pro-3.101.043": dict(
        lookup=0x0c7d5950, loglevel=0x0c7d4708, temp_alloc=0x0c6b9cfc,
        temp_free=0x0c6b9cf0, memcpy=0x0c588db0, sar_alloc=0x0c7d4ad8,
        wake=0x0c7d4fb4),
    "xiaomi-band-11-4.100.155": dict(
        lookup=0x0c98dc6c, loglevel=0x0c98ca20, temp_alloc=0x0c6d2e26,
        temp_free=0x0c6d2e1c, memcpy=0x0c721210, sar_alloc=0x0c98cdf0,
        wake=0x0c98d2cc),
    "xiaomi-band-11-4.100.179": dict(
        lookup=0x0c98bfe4, loglevel=0x0c98ad98, temp_alloc=0x0c6d1212,
        temp_free=0x0c6d1208, memcpy=0x0c721410, sar_alloc=0x0c98b168,
        wake=0x0c98b644),
}


class NativeSend:
    SESSION, CONNECTION, QUEUE = 0x20010000, 0x20011000, 0x20012000
    TEMP, OWNED, DATA = 0x20013000, 0x20014000, 0x20015000
    PACKET, PAYLOAD, NODE = 0x20016000, 0x20017000, 0x20018000
    STACK, STOP = 0x200ff000, 0x10000

    def __init__(self, case, target, fault=None):
        self.case, self.fault = case, fault
        evidence = json.loads((ROOT / "targets" / target / "evidence/l2-sram-probe.json").read_text())
        firmware = fwlocate.require(target).read_bytes()
        case.assertEqual(hashlib.sha256(firmware).hexdigest(), evidence["firmware_sha256"])
        self.address = {k: int(v, 0) for k, v in evidence["addresses"].items()}
        self.profile = PROFILES[target]
        u = self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        size = (len(firmware) + 4095) & ~4095
        for base in (0x0c0c0000, 0x2c0c0000):
            u.mem_map(base, size)
            u.mem_write(base, firmware)
        u.mem_map(0x20000000, 0x100000)
        u.mem_map(self.STOP, 4096)
        self.sar_allocations, self.wakes = 0, 0
        # Active encrypted SAR session, with a single live connection and empty TX list.
        self.half(self.SESSION + 14, 7)
        self.half(self.SESSION + 16, 2)
        head = self.address["CONNECTIONS"]
        self.words(head, self.NODE, self.NODE)
        self.words(self.NODE, head, head, 0, self.CONNECTION)
        self.half(self.CONNECTION + 6, 7)
        u.mem_write(self.CONNECTION + 0x4f, bytes([fault != "inactive"]))
        self.half(self.CONNECTION + 0x52, 0xffff)
        self.words(self.CONNECTION + 0x28, self.QUEUE)
        self.words(self.QUEUE, self.QUEUE, self.QUEUE)
        for name in ("lookup", "loglevel", "temp_alloc", "temp_free", "memcpy", "sar_alloc", "wake"):
            address = self.profile[name]
            u.hook_add(UC_HOOK_CODE, self.boundary, name, address, address)
        crypto = self.address["CRYPTO"] & ~1
        u.hook_add(UC_HOOK_CODE, self.boundary, "crypto", crypto, crypto)

    def reg(self, n):
        return self.u.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[n])

    def words(self, address, *values):
        self.u.mem_write(address, struct.pack("<" + "I" * len(values), *values))

    def word(self, address):
        return struct.unpack("<I", self.u.mem_read(address, 4))[0]

    def half(self, address, value):
        self.u.mem_write(address, struct.pack("<H", value))

    def boundary(self, u, address, size, name):
        result = 0
        if name == "temp_alloc":
            self.case.assertEqual(self.reg(0), 1)
            result = 0 if self.fault == "temp_alloc" else self.TEMP
        elif name == "crypto":
            self.case.assertEqual((self.reg(0), self.reg(1)), (self.SESSION, 1))
            output = self.word(u.reg_read(UC_ARM_REG_SP))
            if self.fault == "crypto":
                result = -1
            else:
                u.mem_write(output, bytes(x ^ 0xa5 for x in u.mem_read(self.reg(3), self.reg(2))))
        elif name == "memcpy":
            u.mem_write(self.reg(0), bytes(u.mem_read(self.reg(1), self.reg(2))))
            result = self.reg(0)
        elif name == "sar_alloc":
            self.sar_allocations += 1
            self.case.assertEqual(self.reg(0), 3)
            if self.fault != "sar_alloc":
                self.words(self.OWNED + 16, self.DATA)
                u.mem_write(self.DATA + 2, b"\x03")
                self.half(self.DATA + 4, self.reg(1))
                result = self.OWNED
        elif name == "wake":
            self.wakes += 1
        u.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def send(self, plaintext):
        u = self.u
        u.mem_write(self.PAYLOAD, plaintext)
        u.mem_write(self.PACKET, struct.pack("<BBHIHH4I", 0x43, 2, 0, self.PAYLOAD,
                                            len(plaintext), 0, 0, 0, 0, 0))
        u.reg_write(UC_ARM_REG_R0, self.SESSION)
        u.reg_write(UC_ARM_REG_R1, self.PACKET)
        u.reg_write(UC_ARM_REG_SP, self.STACK)
        u.reg_write(UC_ARM_REG_LR, self.STOP | 1)
        u.emu_start(self.address["SEND"], self.STOP, count=100000)
        self.case.assertEqual(u.reg_read(UC_ARM_REG_PC), self.STOP)
        value = u.reg_read(UC_ARM_REG_R0)
        return value if value < 0x80000000 else value - 0x100000000


@unittest.skipIf(Uc is None, "unicorn not installed; native L2 send execution skipped")
class NativeSendTests(unittest.TestCase):
    def check_target(self, target):
        plaintext = bytes(range(256)) * 2 + bytes(range(36))  # maximum CPC2 frame: 548
        ciphertext = bytes(x ^ 0xa5 for x in plaintext)
        for fault in (None, "sar_alloc", "inactive", "temp_alloc", "crypto"):
            with self.subTest(target=target, fault=fault):
                m = NativeSend(self, target, fault)
                self.assertEqual(m.send(plaintext), 0 if fault is None else -1)
                changed = fault not in ("temp_alloc", "crypto")
                self.assertEqual(bytes(m.u.mem_read(m.PAYLOAD, len(plaintext))),
                                 ciphertext if changed else plaintext)
                if fault is None:
                    self.assertEqual((m.word(m.QUEUE), m.word(m.QUEUE + 4)), (m.OWNED, m.OWNED))
                    self.assertEqual(bytes(m.u.mem_read(m.DATA + 8, 2)), b"\x43\x02")
                    self.assertEqual(bytes(m.u.mem_read(m.DATA + 10, len(plaintext))), ciphertext)
                    # The queue owns a copy: caller storage can be destroyed immediately.
                    m.u.mem_write(m.PAYLOAD, bytes(len(plaintext)))
                    self.assertEqual(bytes(m.u.mem_read(m.DATA + 10, len(plaintext))), ciphertext)
                    self.assertEqual(m.wakes, 1)
                else:
                    self.assertEqual((m.word(m.QUEUE), m.word(m.QUEUE + 4)), (m.QUEUE, m.QUEUE))
                    self.assertEqual(m.wakes, 0)
                self.assertEqual(m.sar_allocations, int(fault in (None, "sar_alloc")))

    def test_043_send_and_sar_return_ownership(self):
        self.check_target("xiaomi-band-10-pro-3.101.043")

    def test_155_send_and_sar_return_ownership(self):
        self.check_target("xiaomi-band-11-4.100.155")

    def test_179_send_and_sar_return_ownership(self):
        self.check_target("xiaomi-band-11-4.100.179")


if __name__ == "__main__":
    unittest.main()
