.syntax unified
.thumb

#ifndef CANOPUS_BAND9_CAVE_RESULT
#error "CANOPUS_BAND9_CAVE_RESULT is required"
#endif

.section .text.band9_cave,"ax",%progbits
.global canopus_band9_cave_call
.type canopus_band9_cave_call,%function
.thumb_func
canopus_band9_cave_call:
    ldr r0, 1f
    ldr r1, 2f
    ldr r3, 3f
    blx r3
    ldr r2, 4f
    str r0, [r2]
    bx lr
    nop
1:  .word 0
2:  .word 0
3:  .word 0
4:  .word CANOPUS_BAND9_CAVE_RESULT
.size canopus_band9_cave_call, .-canopus_band9_cave_call

.global canopus_band9_cave_barrier
.type canopus_band9_cave_barrier,%function
.thumb_func
canopus_band9_cave_barrier:
    dsb sy
    isb sy
    bx lr
    nop
.size canopus_band9_cave_barrier, .-canopus_band9_cave_barrier
