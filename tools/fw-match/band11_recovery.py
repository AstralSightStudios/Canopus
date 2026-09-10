"""Read-only raw-image helpers for the Band 11 bootstrap/ABI audit.

Virtual mappings are startup-copy ranges, not assumptions about XIP aliases.
This module never opens or modifies an IDB.
"""
from pathlib import Path
import json
import re
import struct
import capstone

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x0C0C0000
TARGET = "xiaomi-band-11-4.100.139"


class Firmware:
    def __init__(self, target=TARGET):
        self.target = target
        self.data = (ROOT / "fwbins" / target / "vela_ap.bin").read_bytes()
        self.functions = json.loads((ROOT / "targets/fw-corpus" / (target + ".json")).read_text())["functions"]
        self.by_addr = {int(f["addr"], 16): f for f in self.functions}
        self.maps = [(BASE, len(self.data), 0), (BASE + 0x20000000, len(self.data), 0)]
        if target == TARGET:
            # 0x0C0C01A0, 0x0C0C01E4, 0x0C0C024C startup copy loops.
            self.add_copy(0x2006BE00, 0xB710, 0x2C0C1B3C)
            self.add_copy(0x20079420, 0x2F8C, 0x2C0CD24C)
            self.add_copy(0x3C000000, 0x256B40, 0x2C0D01E0)
            self.add_copy(0x2007DCC8, 0x200ADEDC - 0x2007DCC8, 0x2CC4BE64)
        elif target == "xiaomi-band-10-pro-3.101.043":
            self.add_copy(0x200765C0, 0x3A328, 0x2C0C23A4)
            self.add_copy(0x200B2860, 0x10D6C, 0x2C0FC6CC)
            self.add_copy(0x3C000000, 0x80500, 0x2C10D440)
        self.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
        self.md.skipdata = True

    def add_copy(self, destination, size, source):
        offset = source - 0x2C0C0000
        self.maps.extend([(destination, size, offset), (destination - 0x20000000 if destination >= 0x3C000000 else destination - 0x1FE00000, size, offset)])

    def offset(self, address):
        for base, size, offset in self.maps:
            if base <= address < base + size:
                return offset + address - base
        raise ValueError(f"unmapped {address:#x}")

    def read(self, address, size):
        offset = self.offset(address)
        return self.data[offset:offset + size]

    def word(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def string(self, address):
        try:
            raw = self.read(address, 160).split(b"\0", 1)[0]
        except ValueError:
            return ""
        return repr(raw.decode()) if raw and all(9 <= c <= 13 or 32 <= c < 127 for c in raw) else ""

    def resolve(self, address):
        address &= ~1
        if self.read(address, 4) == bytes.fromhex("5ff800f0"):
            return self.word(address + 4) & ~1
        return address

    def file_address(self, address):
        return BASE + self.offset(address & ~1)

    def execution_address(self, file_address):
        offset = file_address - BASE
        for base, size, start in reversed(self.maps[2:]):
            if start <= offset < start + size:
                return base + offset - start
        return file_address

    def dis(self, address, size=None):
        address &= ~1
        size = size or self.by_addr.get(address, {}).get("size", 80)
        print("DIS", hex(address))
        for ins in self.md.disasm(self.read(address, size), address):
            note = ""
            match = re.fullmatch(r".*\[pc, #(-?0x[0-9a-f]+|-?[0-9]+)\]", ins.op_str)
            if match:
                try:
                    pointer = self.word(((ins.address + 4) & ~3) + int(match[1], 0))
                    note = f"; {pointer:#x} {self.string(pointer)}"
                except ValueError:
                    pass
            print(f"{ins.address:#x} {ins.mnemonic} {ins.op_str} {note}")

    def refs(self, pointer):
        return [BASE + m.start() for m in re.finditer(re.escape(struct.pack("<I", pointer)), self.data)]

    def find(self, string):
        return [f for f in self.functions if any(string in s for s in f["strings"])]

    def callers(self, address):
        return self.by_addr.get(address, {}).get("callers", [])


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", type=lambda x: int(x, 0))
    parser.add_argument("size", type=lambda x: int(x, 0), nargs="?")
    parser.add_argument("--target", default=TARGET)
    args = parser.parse_args()
    Firmware(args.target).dis(args.address, args.size)
