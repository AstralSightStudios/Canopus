//! Thumb-2 entry-pattern relocation masker.
//!
//! The pattern layer compares the first bytes of a function across builds.
//! Exact bytes are unstable because a rebuild changes branch offsets, literal
//! pools and address immediates. This module computes a per-byte mask: bit = 1
//! means "compare this bit", bit = 0 means "ignore it (relocation-sensitive)".
//!
//! Scope: the common ARM/Thumb-2 encodings that move when a function is
//! relocated or a neighboring function shifts. Everything unclassified keeps
//! all bits, so a wrong mask never discards opcode identity — it only risks
//! keeping a relocated operand, which the CFG/xref/GA layers then disambiguate.

/// Mask bit is 1 = compare this byte's bits, 0 = ignore.
/// For simplicity we produce a byte mask per input byte (0x00 = ignore all,
/// 0xFF = compare all); nibble-precision masks are used where a halfword mixes
/// an opcode field and an immediate field.
pub fn entry_mask(bytes: &[u8]) -> Vec<u8> {
    let mut out = vec![0xFF; bytes.len()];
    let mut i = 0;
    // Eight-byte import veneers: `5f f8 00 f0 <target>` = LDR.W R12,[PC,#0]
    // followed by the literal target. The literal value is a relocated
    // absolute address (RAM/alias), so it must never be compared across
    // builds — the veneer's identity is in its callers, not the target bytes.
    if bytes.len() >= 8 && bytes[..4] == [0x5f, 0xf8, 0x00, 0xf0] {
        for b in out.iter_mut().take(4) {
            *b = 0xFF; // opcode: keep
        }
        for b in out.iter_mut().skip(4).take(4) {
            *b = 0x00; // literal target: mask
        }
        return out;
    }
    while i + 2 <= bytes.len() {
        let lo = bytes[i] as u16;
        let hi = bytes[i + 1] as u16;
        let hw = (hi << 8) | lo;
        // Thumb-2 32-bit prefix: top bits 11101 / 11110 / 11111.
        // (0b11101_00000000000 = 0xE800; 0b11110/11111 prefixes -> 0xF000+)
        let is32 = (hw & 0xE000) == 0xE000 && (hw & 0xF800) != 0xE000; // 11101/11110/11111
        if is32 {
            if i + 4 > bytes.len() {
                break;
            }
            let lo2 = bytes[i + 2] as u16;
            let hi2 = bytes[i + 3] as u16;
            let hw2 = (hi2 << 8) | lo2;
            mask32(&mut out[i..i + 4], hw, hw2);
            i += 4;
        } else {
            mask16(&mut out[i..i + 2], hw);
            i += 2;
        }
    }
    out
}

/// 16-bit Thumb instruction masks.
fn mask16(out: &mut [u8], hw: u16) {
    // B (unconditional): 11100 imm11 -> imm is the 11-bit target offset.
    if hw & 0xF800 == 0xE000 {
        // keep 11100 (bits 15:11), mask imm11 (bits 10:0).
        // byte0 = bits 7:0 (all imm), byte1 = bits 15:8 (11100 + imm top 3)
        out[0] = 0x00;
        out[1] = 0xF8;
        return;
    }
    // B.cond: 1101 cond imm8
    if hw & 0xF000 == 0xD000 {
        // byte0 = imm8, byte1 = 1101 cond
        out[0] = 0x00;
        out[1] = 0xF0;
        return;
    }
    // LDR literal Rt, imm8: 01001 Rt imm8
    if hw & 0xF800 == 0x4800 {
        // byte0 = imm8, byte1 = 01001 Rt (Rt is a register number, keep)
        out[0] = 0x00;
        out[1] = 0xFF;
        return;
    }
    // BLX/BX register variants 01000111 xxxxxxxx: no relocation operand, keep.
    // Everything else: keep all bits.
}

/// 32-bit Thumb-2 instruction masks.
fn mask32(out: &mut [u8], hw: u16, hw2: u16) {
    // BL/BLX: halfword1 = 1111 0 S 1 H imm10, halfword2 = 11 J1 1 J2 imm11
    // (BL) or 11 0 J1 0 J2 imm11 (BLX). Keep the 11110 prefix + the BL/BLX
    // selector bit; mask S, J1, J2 and both immediate halves.
    if hw & 0xF800 == 0xF000 && hw2 & 0xC000 == 0xC000 {
        out[0] = 0x00; // imm10
        out[1] = 0xFB; // 1111 0 _ 1 H  (keep 11110, bit9, bit8)
        out[2] = 0x00; // imm11 low byte
        out[3] = 0xC0; // 11.. (keep bits 15:14), J1/J2/imm11 masked
        return;
    }
    // MOVW: 1111 0 i 10 0100 imm4 | 0 imm3 Rd imm8
    // MOVT: 1111 0 i 10 1100 imm4 | 0 imm3 Rd imm8
    if hw & 0xFBF0 == 0xF240 || hw & 0xFBF0 == 0xF2C0 {
        // first halfword: keep 1111 0 _ 10 0100 (bits), mask i (bit10) + imm4
        //   byte0 = bits 7:0 = 0100 imm4 -> keep 0100 (0xF0), mask imm4
        //   byte1 = bits 15:8 = 1111 0 i 10 -> keep 1111 0 _ 10 (0xFB), mask bit10
        out[0] = 0xF0;
        out[1] = 0xFB;
        // second halfword: 0 imm3 Rd imm8 -> byte2 = imm8 (mask), byte3 = 0 imm3 Rd
        out[2] = 0x00;
        out[3] = 0x8F; // keep bit15(0) + Rd(11:8); mask imm3(14:12)
        return;
    }
    // ADDW: 1111 0 i 10 1000 imm4 | 0 imm3 Rn Rd imm12
    // SUBW: 1111 0 i 10 1010 imm4 | 0 imm3 Rn Rd imm12
    if hw & 0xFBF0 == 0xF280 || hw & 0xFBF0 == 0xF2A0 {
        out[0] = 0xF0;
        out[1] = 0xFB;
        out[2] = 0x00;
        out[3] = 0x8F;
        return;
    }
    // ADR.W: 1111 0 i 10 0000 imm4 | 0 imm3 Rd imm12
    if hw & 0xFBF0 == 0xF200 {
        out[0] = 0xF0;
        out[1] = 0xFB;
        out[2] = 0x00;
        out[3] = 0x8F;
        return;
    }
    // LDR.W literal Rt, imm12: 1111 1000 U 101 Rt imm12
    if hw & 0xFF70 == 0xF850 {
        // first halfword: keep 1111 1000 _ 101 Rt; mask U (bit7)
        //   byte0 = bits 7:0 = U 101 Rt -> keep 101 Rt (0x70), mask U
        //   byte1 = 1111 1000 -> keep
        out[0] = 0x70;
        out[1] = 0xFF;
        out[2] = 0x00; // imm12 low byte
        out[3] = 0x00; // imm12 high byte
        return;
    }
    // Everything else: keep all bits (conservative).
}

/// Compare two entry byte sequences under a mask (applies `mask` computed
/// against the *source* bytes). Returns (matched_bits, total_bits).
pub fn masked_compare(a: &[u8], b: &[u8], mask: &[u8]) -> (u64, u64) {
    let n = a.len().min(b.len()).min(mask.len());
    let mut matched = 0u64;
    let mut total = 0u64;
    for i in 0..n {
        let m = mask[i];
        if m == 0 {
            continue;
        }
        let x = (a[i] ^ b[i]) & m;
        // count matched bits in this byte
        let mut mm = m;
        while mm != 0 {
            total += 1;
            if (x & (mm & (!mm + 1))) == 0 {
                matched += 1;
            }
            mm &= mm - 1;
        }
    }
    (matched, total)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn b_unconditional_masks_imm() {
        // B #0x200 -> 0xE200 (unconditional B, imm11 = 0x200>>1 = 0x100...)
        // 11100_10000000000 = 0xE400 (target +0x400). bytes LE: 00 E4
        let bytes = [0x00u8, 0xE4];
        let m = entry_mask(&bytes);
        assert_eq!(m[0], 0x00);
        assert_eq!(m[1], 0xF8);
    }

    #[test]
    fn bcond_masks_imm() {
        // BEQ #-8 -> cond 0000, imm8 = -8>>1 = 0xFC -> 0xD0FC? byte1 = 1101 0000
        let bytes = [0xFCu8, 0xD0];
        let m = entry_mask(&bytes);
        assert_eq!(m[0], 0x00);
        assert_eq!(m[1], 0xF0);
    }

    #[test]
    fn bl_masks_all() {
        // BL: 11110 P 1 H imm10 | 11 J1 1 J2 imm11 -> e.g. 0xF000 0xF000
        let bytes = [0x00u8, 0xF0, 0x00, 0xF0];
        let m = entry_mask(&bytes);
        assert_eq!(m[0], 0x00); // imm10 masked
        assert_eq!(m[2], 0x00); // imm11 low masked
        // keep 11110 prefix (0xF8) + bit9(1)=0x02 + bit8(H)=0x01
        assert_eq!(m[1], 0xFB);
        // second halfword: keep 11.. bits 15:14 only
        assert_eq!(m[3], 0xC0);
    }

    #[test]
    fn movw_masks_imm() {
        // MOVW R0, #0x1234: first hw = 0xF240 | i<<10=0 | imm4(0x1) -> 0xF241
        // second hw = 0 | imm3(0x2)<<12 | Rd(0)<<8 | imm8(0x34) -> 0x2000 | 0x34 = 0x2034
        let bytes = [0x41u8, 0xF2, 0x34, 0x20];
        let m = entry_mask(&bytes);
        assert_eq!(m[0], 0xF0, "keep 0100 opcode nibble, mask imm4");
        assert_eq!(m[2], 0x00, "mask imm8");
        assert_eq!(m[3], 0x8F, "keep 0 + Rd, mask imm3");
    }

    #[test]
    fn ldr_literal_16_masks_imm() {
        // LDR R0, [PC, #0x10]: 01001 000 00010000 = 0x4810
        let bytes = [0x10u8, 0x48];
        let m = entry_mask(&bytes);
        assert_eq!(m[0], 0x00);
        assert_eq!(m[1], 0xFF);
    }

    #[test]
    fn ldr32_literal_masks_second_half() {
        // LDR.W R0, [PC, #imm12]: 1111 1000 0 101 0000 | imm12
        //   = 0xF850 with Rt=0, U=0.
        let bytes = [0x50u8, 0xF8, 0x10, 0x00];
        let m = entry_mask(&bytes);
        assert_eq!(m[2], 0x00);
        assert_eq!(m[3], 0x00);
        assert_eq!(m[0], 0x70); // keep 101 Rt, mask U bit
        assert_eq!(m[1], 0xFF);
    }

    #[test]
    fn unknown_keeps_all() {
        // PUSH {r4-r7,lr}: 0xB5F0
        let bytes = [0xF0u8, 0xB5];
        let m = entry_mask(&bytes);
        assert_eq!(m, vec![0xFF, 0xFF]);
    }

    #[test]
    fn tiny_thunk_masks_literal_target() {
        // 5f f8 00 f0 <target>: LDR.W R12,[PC,#0]; BX R12 veneer.
        // Two veneers to *different* targets must look identical to pattern.
        let a = [0x5fu8, 0xf8, 0x00, 0xf0, 0x21, 0x8c, 0x29, 0x00];
        let b = [0x5fu8, 0xf8, 0x00, 0xf0, 0xa9, 0x00, 0x06, 0x1c];
        let m = entry_mask(&a);
        assert_eq!(m[0], 0xFF);
        assert_eq!(m[1], 0xFF);
        assert_eq!(m[4], 0x00, "literal target must be masked");
        assert_eq!(m[7], 0x00);
        let (matched, total) = masked_compare(&a, &b, &m);
        assert_eq!(matched, total, "different veneer targets must score identical");
    }

    #[test]
    fn masked_compare_counts() {
        // B +0x200 (0xE400) vs B +0x100 (0xE200): only the imm11 differs,
        // which the mask ignores. byte0 fully masked, byte1 keeps 11100 (5 bits).
        let a = [0x00u8, 0xE4];
        let b = [0x00u8, 0xE2];
        let m = entry_mask(&a);
        let (matched, total) = masked_compare(&a, &b, &m);
        assert_eq!(matched, 5);
        assert_eq!(total, 5);
    }

    #[test]
    fn masked_compare_detects_opcode_difference() {
        let a = [0x00u8, 0xE4]; // B
        let b = [0xF0u8, 0xB5]; // PUSH {r4-r7, lr}
        let m = entry_mask(&a);
        let (matched, total) = masked_compare(&a, &b, &m);
        assert!(matched < total);
        assert!(matched < 5, "opcode change must lose masked bits, got {matched}");
    }
}
