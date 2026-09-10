.syntax unified
.thumb
.section .text.entry,"ax",%progbits
.global canopus_band11_stage1_entry
.type canopus_band11_stage1_entry,%function
.thumb_func
canopus_band11_stage1_entry:
    b.w 1f
/* Patched only inside the Lua-owned long-string payload, never its GC header. */
.Lmailbox:
    .word 0 /* data-alias mailbox address, file offset 4 */
1:  push {r4, lr}
    adr r4, .Lmailbox
    ldr r4, [r4]
    mov r0, r4
    bl canopus_band11_stage1
    str r0, [r4]
    dsb sy
    pop {r4, pc}
.size canopus_band11_stage1_entry, .-canopus_band11_stage1_entry
