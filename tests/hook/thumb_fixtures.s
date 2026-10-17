/* Instruction fixtures for the actual (!CANOPUS_HOST) Thumb Hook Engine. */
.syntax unified
.cpu cortex-m33
.thumb
.text

/* The MOVW second halfword is 0x4770 (BX LR if decoded out of context).
 * Scanning six 16-bit bytes then this 32-bit instruction must copy ten bytes. */
.balign 4
.global fixture_aligned
.thumb_func
fixture_aligned:
    push {r7}
    adds r0, #1
    nop
    movw r7, #0x0470
    adds r0, #9
    pop {r7}
    bx lr

/* Halfword-aligned entry needs a ten-byte patch, rounded to a complete
 * twelve-byte instruction prefix. */
.balign 4
    nop
.global fixture_unaligned
.thumb_func
fixture_unaligned:
    push {r7}
    movw r7, #0x0470
    nop
    movt r7, #0x1234
    adds r0, #6
    pop {r7}
    bx lr

.balign 4
.global fixture_wide_push
.thumb_func
fixture_wide_push:
    push.w {r4-r8, lr}
    adds r0, #1
    nop
    adds r0, #2
    pop.w {r4-r8, pc}

.balign 4
.global fixture_replacement
.thumb_func
fixture_replacement:
    adds r0, #100
    bx lr

.balign 4
.global fixture_pc_read
.thumb_func
fixture_pc_read:
    mov r0, pc
    nop
    nop
    nop
    bx lr

.balign 4
.global fixture_mov_pc_lr
.thumb_func
fixture_mov_pc_lr:
    mov pc, lr
    nop
    nop
    nop
    bx lr

.balign 4
.global fixture_return16
.thumb_func
fixture_return16:
    bx lr
    nop
    nop
    nop
    nop

.balign 4
.global fixture_return32
.thumb_func
fixture_return32:
    ldr.w pc, [sp], #4
    nop
    nop
    nop

/* An IT at the last halfword of the patch would predicate the trampoline's
 * alignment NOP/jump, leaving the real conditional instruction at the source. */
.balign 4
.global fixture_it_split
.thumb_func
fixture_it_split:
    cmp r0, #0
    nop
    nop
    it eq
    moveq r0, #7
    bx lr

/* Multi-instruction IT straddling the halfword-aligned ten-byte patch. */
.balign 4
    nop
.global fixture_it_unaligned
.thumb_func
fixture_it_unaligned:
    cmp r0, #0
    nop
    nop
    itt eq
    moveq r0, #7
    addeq r0, #1
    bx lr

.balign 4
.global fixture_literal_boundary
.thumb_func
fixture_literal_boundary:
    nop
    nop
    nop
    ldr.w r0, [pc, #4]
    bx lr
    .word 17

/* PC-independent but deliberately not implemented by the copy-safe allowlist. */
.balign 4
.global fixture_unimplemented32
.thumb_func
fixture_unimplemented32:
    udiv r0, r1, r2
    nop
    nop
    bx lr
