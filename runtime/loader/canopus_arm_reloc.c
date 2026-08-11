#include "canopus_arm_reloc.h"

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static int decode_thumb_mov(const uint8_t *place, uint32_t type,
                            uint16_t *imm16)
{
    uint16_t first = read_u16(place);
    uint16_t second = read_u16(place + 2);
    uint16_t opcode = type == CANOPUS_R_ARM_THM_MOVW_ABS_NC
                          ? UINT16_C(0xF240)
                          : UINT16_C(0xF2C0);

    if ((first & UINT16_C(0xFBF0)) != opcode ||
        (second & UINT16_C(0x8000)) != 0) {
        return CANOPUS_RELOC_INVALID;
    }
    *imm16 = (uint16_t)(((first & UINT16_C(0x000F)) << 12) |
                        ((first & UINT16_C(0x0400)) << 1) |
                        ((second & UINT16_C(0x7000)) >> 4) |
                        (second & UINT16_C(0x00FF)));
    return CANOPUS_RELOC_OK;
}

static void encode_thumb_mov(uint8_t *place, uint16_t imm16)
{
    uint16_t first = read_u16(place);
    uint16_t second = read_u16(place + 2);

    first = (uint16_t)((first & UINT16_C(0xFBE0)) |
                       ((imm16 >> 12) & UINT16_C(0x000F)) |
                       ((imm16 >> 1) & UINT16_C(0x0400)));
    second = (uint16_t)((second & UINT16_C(0x8F00)) |
                        ((imm16 << 4) & UINT16_C(0x7000)) |
                        (imm16 & UINT16_C(0x00FF)));
    write_u16(place, first);
    write_u16(place + 2, second);
}

static int decode_thumb_branch(const uint8_t *place, uint32_t type,
                               int32_t *addend)
{
    uint16_t first = read_u16(place);
    uint16_t second = read_u16(place + 2);
    uint32_t second_opcode = type == CANOPUS_R_ARM_THM_CALL
                                 ? UINT16_C(0xD000)
                                 : UINT16_C(0x9000);
    uint32_t s;
    uint32_t j1;
    uint32_t j2;
    uint32_t i1;
    uint32_t i2;
    uint32_t value;

    if ((first & UINT16_C(0xF800)) != UINT16_C(0xF000) ||
        (second & UINT16_C(0xD000)) != second_opcode) {
        return CANOPUS_RELOC_INVALID;
    }
    s = (first >> 10) & 1u;
    j1 = (second >> 13) & 1u;
    j2 = (second >> 11) & 1u;
    i1 = !(j1 ^ s);
    i2 = !(j2 ^ s);
    value = (s << 24) | (i1 << 23) | (i2 << 22) |
            ((uint32_t)(first & UINT16_C(0x03FF)) << 12) |
            ((uint32_t)(second & UINT16_C(0x07FF)) << 1);
    *addend = (int32_t)(value << 7) >> 7;
    return CANOPUS_RELOC_OK;
}

static void encode_thumb_branch(uint8_t *place, int32_t displacement)
{
    uint16_t first = read_u16(place);
    uint16_t second = read_u16(place + 2);
    uint32_t value = (uint32_t)displacement;
    uint32_t s = (value >> 24) & 1u;
    uint32_t i1 = (value >> 23) & 1u;
    uint32_t i2 = (value >> 22) & 1u;
    uint32_t j1 = !(i1 ^ s);
    uint32_t j2 = !(i2 ^ s);

    first = (uint16_t)((first & UINT16_C(0xF800)) | (s << 10) |
                       ((value >> 12) & UINT16_C(0x03FF)));
    second = (uint16_t)((second & UINT16_C(0xD000)) | (j1 << 13) |
                        (j2 << 11) |
                        ((value >> 1) & UINT16_C(0x07FF)));
    write_u16(place, first);
    write_u16(place + 2, second);
}

int canopus_arm_rel_apply(uint32_t type, uint8_t *place,
                          uint32_t symbol, uint32_t place_addr)
{
    uint32_t addend;
    uint32_t value;
    int32_t branch_addend;
    int32_t displacement;
    uint16_t imm16;
    int rc;

    if (type == CANOPUS_R_ARM_NONE) {
        return CANOPUS_RELOC_OK;
    }
    if (place == 0) {
        return CANOPUS_RELOC_INVALID;
    }

    switch (type) {
    case CANOPUS_R_ARM_ABS32:
    case CANOPUS_R_ARM_TARGET1:
        addend = read_u32(place);
        write_u32(place, symbol + addend);
        return CANOPUS_RELOC_OK;
    case CANOPUS_R_ARM_REL32:
        addend = read_u32(place);
        write_u32(place, symbol + addend - place_addr);
        return CANOPUS_RELOC_OK;
    case CANOPUS_R_ARM_PREL31:
        addend = read_u32(place);
        value = symbol + (uint32_t)((int32_t)(addend << 1) >> 1) -
                place_addr;
        if (value != (uint32_t)((int32_t)(value << 1) >> 1)) {
            return CANOPUS_RELOC_OVERFLOW;
        }
        write_u32(place, (addend & UINT32_C(0x80000000)) |
                         (value & UINT32_C(0x7FFFFFFF)));
        return CANOPUS_RELOC_OK;
    case CANOPUS_R_ARM_THM_MOVW_ABS_NC:
        rc = decode_thumb_mov(place, type, &imm16);
        if (rc != CANOPUS_RELOC_OK) {
            return rc;
        }
        encode_thumb_mov(place, (uint16_t)(symbol + imm16));
        return CANOPUS_RELOC_OK;
    case CANOPUS_R_ARM_THM_MOVT_ABS:
        rc = decode_thumb_mov(place, type, &imm16);
        if (rc != CANOPUS_RELOC_OK) {
            return rc;
        }
        value = symbol + ((uint32_t)imm16 << 16);
        encode_thumb_mov(place, (uint16_t)(value >> 16));
        return CANOPUS_RELOC_OK;
    case CANOPUS_R_ARM_THM_CALL:
    case CANOPUS_R_ARM_THM_JUMP24:
        rc = decode_thumb_branch(place, type, &branch_addend);
        if (rc != CANOPUS_RELOC_OK) {
            return rc;
        }
        displacement = (int32_t)((symbol & ~UINT32_C(1)) +
                                 (uint32_t)branch_addend - place_addr);
        if ((displacement & 1) != 0 || displacement < -16777216 ||
            displacement > 16777214) {
            return CANOPUS_RELOC_OVERFLOW;
        }
        encode_thumb_branch(place, displacement);
        return CANOPUS_RELOC_OK;
    default:
        return CANOPUS_RELOC_UNSUPPORTED;
    }
}
