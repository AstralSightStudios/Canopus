/* ARM ELF32 REL relocation primitives for the portable Canopus loader. */
#ifndef CANOPUS_ARM_RELOC_H
#define CANOPUS_ARM_RELOC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_R_ARM_NONE             0u
#define CANOPUS_R_ARM_THM_CALL        10u
#define CANOPUS_R_ARM_ABS32            2u
#define CANOPUS_R_ARM_REL32           23u
#define CANOPUS_R_ARM_THM_JUMP24      30u
#define CANOPUS_R_ARM_TARGET1         38u
#define CANOPUS_R_ARM_PREL31          42u
#define CANOPUS_R_ARM_THM_MOVW_ABS_NC 47u
#define CANOPUS_R_ARM_THM_MOVT_ABS    48u

#define CANOPUS_RELOC_OK           0
#define CANOPUS_RELOC_INVALID     -1
#define CANOPUS_RELOC_UNSUPPORTED -2
#define CANOPUS_RELOC_OVERFLOW    -3

/* Applies one ELF32 ARM SHT_REL relocation in place. `symbol` and `place_addr`
 * are target virtual addresses; the implicit addend is decoded from `place`.
 * The caller must provide at least four writable bytes at `place`. */
int canopus_arm_rel_apply(uint32_t type, uint8_t *place,
                          uint32_t symbol, uint32_t place_addr);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_ARM_RELOC_H */
