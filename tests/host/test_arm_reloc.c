/* Host tests for the freestanding ARM ELF32 relocation core. */
#include "canopus_test.h"
#include "canopus_arm_reloc.h"

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void encode_mov(uint8_t *p, uint16_t opcode, uint16_t rd,
                       uint16_t imm16)
{
    uint16_t first = (uint16_t)(opcode | ((imm16 >> 1) & 0x0400u) |
                                ((imm16 >> 12) & 0x000fu));
    uint16_t second = (uint16_t)(((imm16 << 4) & 0x7000u) |
                                 (rd << 8) | (imm16 & 0x00ffu));
    p[0] = (uint8_t)first;
    p[1] = (uint8_t)(first >> 8);
    p[2] = (uint8_t)second;
    p[3] = (uint8_t)(second >> 8);
}

static uint16_t decode_mov(const uint8_t *p)
{
    uint16_t first = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    uint16_t second = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
    return (uint16_t)(((first & 0x000fu) << 12) |
                      ((first & 0x0400u) << 1) |
                      ((second & 0x7000u) >> 4) |
                      (second & 0x00ffu));
}

TEST(arm_reloc_word_types_apply_implicit_addends)
{
    uint8_t word[4];

    write_le32(word, 4u);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_ABS32, word,
                                0x20001000u, 0x20000000u) == 0);
    CHECK(read_le32(word) == 0x20001004u);

    write_le32(word, UINT32_C(0xfffffff8));
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_REL32, word,
                                0x20001100u, 0x20001000u) == 0);
    CHECK(read_le32(word) == 0x000000f8u);

    write_le32(word, 8u);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_TARGET1, word,
                                0x0c100001u, 0) == 0);
    CHECK(read_le32(word) == 0x0c100009u);
}

TEST(arm_reloc_prel31_preserves_flag_and_checks_range)
{
    uint8_t word[4];

    write_le32(word, UINT32_C(0x80000004));
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_PREL31, word,
                                0x20001100u, 0x20001000u) == 0);
    CHECK(read_le32(word) == UINT32_C(0x80000104));

    write_le32(word, 0);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_PREL31, word,
                                UINT32_C(0x80000000), 0) ==
          CANOPUS_RELOC_OVERFLOW);
}

TEST(arm_reloc_thumb_movw_movt_update_immediates)
{
    uint8_t movw[4];
    uint8_t movt[4];

    encode_mov(movw, 0xf240u, 3u, 0x0123u);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_THM_MOVW_ABS_NC, movw,
                                0x0c100010u, 0) == 0);
    CHECK(decode_mov(movw) == 0x0133u);
    CHECK((movw[3] & 0x0fu) == 3u);

    encode_mov(movt, 0xf2c0u, 7u, 0x0001u);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_THM_MOVT_ABS, movt,
                                0x0c10ffffu, 0) == 0);
    CHECK(decode_mov(movt) == 0x0c11u);
    CHECK((movt[3] & 0x0fu) == 7u);
}

TEST(arm_reloc_rejects_unimplemented_and_malformed_types)
{
    uint8_t place[4] = { 0, 0, 0, 0 };

    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_THM_CALL, place, 0, 0) ==
          CANOPUS_RELOC_UNSUPPORTED);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_THM_JUMP24, place, 0, 0) ==
          CANOPUS_RELOC_UNSUPPORTED);
    CHECK(canopus_arm_rel_apply(255u, place, 0, 0) ==
          CANOPUS_RELOC_UNSUPPORTED);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_THM_MOVW_ABS_NC, place,
                                0, 0) == CANOPUS_RELOC_INVALID);
    CHECK(canopus_arm_rel_apply(CANOPUS_R_ARM_ABS32, 0, 0, 0) ==
          CANOPUS_RELOC_INVALID);
}

static struct test_registry arm_reloc_tests[] = {
    { "arm_reloc_word_types_apply_implicit_addends",
      arm_reloc_word_types_apply_implicit_addends_wrapper },
    { "arm_reloc_prel31_preserves_flag_and_checks_range",
      arm_reloc_prel31_preserves_flag_and_checks_range_wrapper },
    { "arm_reloc_thumb_movw_movt_update_immediates",
      arm_reloc_thumb_movw_movt_update_immediates_wrapper },
    { "arm_reloc_rejects_unimplemented_and_malformed_types",
      arm_reloc_rejects_unimplemented_and_malformed_types_wrapper },
};

int run_arm_reloc_tests(void)
{
    RUN_TESTS(arm_reloc_tests,
              sizeof(arm_reloc_tests) / sizeof(arm_reloc_tests[0]));
}
