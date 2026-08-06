/* ARM EABI helper used by the retained Monocypher Ed25519 verifier.
 *
 * The volatile byte loop prevents Clang from lowering this helper back into
 * itself. The verifier only requests aligned clears, but byte stores keep the
 * implementation correct for every EABI caller shape.
 */
#include <stdint.h>

void __aeabi_memclr4(void *destination, uint32_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)destination;
    while (size != 0u) {
        *bytes++ = 0u;
        size--;
    }
}
