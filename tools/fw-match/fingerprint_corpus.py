#!/usr/bin/env python3
"""Add full Thumb instruction fingerprints to an exact-image corpus.

Preserve registers, widths, field offsets, constants, internal branch targets
and block boundaries. Normalize only external control targets and PC-relative
literal locations, retaining their actual values as separate ordered refs.
This is matching evidence, never an ABI approval or a runtime address scanner.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

import capstone
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_REG_PC

BASE = 0x0C0C0000
ALGORITHM = "thumb-full-v1"


def image_offset(address, size, image):
    for base in (BASE, BASE + 0x20000000):
        off = address - base
        if 0 <= off and off + size <= len(image):
            return off
    raise ValueError(f"outside image: {address:#x}")


def pointer_value(value, image):
    try:
        image_offset(value & ~1, 1, image)
        return True
    except ValueError:
        return 0x20000000 <= value < 0x20200000 or 0x1C000000 <= value < 0x1D000000 or 0x38000000 <= value < 0x3D000000


def fingerprint(fn, image, md):
    start = int(fn["addr"], 16)
    blocks = sorted(fn.get("block_offs") or [], key=lambda b: b["off"])
    spans = [(start + b["off"], start + b["off"] + b["size"]) for b in blocks]
    tokens, refs = [], []
    count = 0
    try:
        for begin, end in spans:
            if begin == end:
                continue
            off = image_offset(begin, end - begin, image)
            tokens.append(f"block:{begin-start}:{end-begin}")
            cursor = begin
            for ins in md.disasm(image[off:off + end - begin], begin):
                if ins.address != cursor:
                    return None
                cursor += ins.size
                count += 1
                operands = ins.op_str
                control = ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)
                for op in ins.operands:
                    if control and op.type == ARM_OP_IMM:
                        target = op.imm & 0xFFFFFFFF
                        internal = any(a <= target < b for a, b in spans)
                        replacement = f"rel:{target-start}" if internal else "external"
                        operands = re.sub(r"#(?:0x[0-9a-f]+|[0-9]+)$", replacement, operands)
                        if not internal:
                            refs.append({"off": ins.address-start, "kind": "control", "addr": hex(target)})
                    elif op.type == ARM_OP_MEM and op.mem.base == ARM_REG_PC and ins.mnemonic.startswith("ldr"):
                        literal = ((ins.address + 4) & ~3) + op.mem.disp
                        # Thumb scalar literal loads here are word loads. Keep
                        # byte/halfword/vector forms literal rather than guessing.
                        if ins.mnemonic not in ("ldr", "ldr.w"):
                            continue
                        value = struct.unpack_from('<I', image, image_offset(literal, 4, image))[0]
                        is_ptr = pointer_value(value, image)
                        replacement = "pointer" if is_ptr else f"constant:{value:#x}"
                        operands = re.sub(r"\[pc(?:, #[^\]]+)?\]", f"[{replacement}]", operands)
                        if is_ptr:
                            refs.append({"off": ins.address-start, "kind": "literal", "addr": hex(value)})
                tokens.append(f"{ins.size}:{ins.mnemonic}:{operands}")
            if cursor != end:
                return None
    except (ValueError, struct.error):
        return None
    if not count:
        return None
    return {"algorithm": ALGORITHM, "sha256": hashlib.sha256('\n'.join(tokens).encode()).hexdigest(),
            "instructions": count, "refs": refs}


def enrich(document, image):
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
    md.detail = True
    for fn in document['functions']:
        fp = fingerprint(fn, image, md)
        if fp:
            fn['body_fingerprint'] = fp
        else:
            fn.pop('body_fingerprint', None)
    document['firmware_sha256'] = hashlib.sha256(image).hexdigest()
    return document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    document = json.loads(args.corpus.read_text())
    image = args.firmware.read_bytes()
    # Every function entry must agree with the raw image before enriching.
    for fn in document['functions']:
        entry = bytes.fromhex(fn.get('entry', ''))
        if entry:
            off = image_offset(int(fn['addr'], 16), len(entry), image)
            if image[off:off+len(entry)] != entry:
                raise ValueError(f"corpus/image entry mismatch: {fn['addr']}")
    enrich(document, image)
    temporary = args.output.with_suffix('.tmp')
    temporary.write_text(json.dumps(document, separators=(',', ':')))
    temporary.replace(args.output)
    print(f"fingerprinted {sum('body_fingerprint' in f for f in document['functions'])}/{len(document['functions'])} functions; {document['firmware_sha256']}")


if __name__ == '__main__':
    main()
