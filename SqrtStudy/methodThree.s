
.text
.global fixed_point_sqrt

fixed_point_sqrt:
    push {r4-r10, lr}

    @ r0 = lower 32 bits of input
    @ r1 = upper 32 bits of input

    mov r2, #0      @ result
    mov r3, #0      @ remainder low
    mov r5, #0      @ remainder high
    mov r6, #32     @ 32 iterations

sqrt_loop:
    @ Extract next two bits from upper input
    mov r4, r1, lsr #30

    @ Shift 64-bit remainder left by 2
    mov r7, r3, lsr #30
    orr r5, r7, r5, lsl #2
    mov r3, r3, lsl #2
    orr r3, r3, r4

    @ Shift 64-bit input left by 2
    mov r7, r0, lsr #30
    orr r1, r7, r1, lsl #2
    mov r0, r0, lsl #2

    @ Shift result left by 1
    mov r2, r2, lsl #1

    @ Calculate 64-bit trial divisor
    @ trial = (result << 1) + 1
    mov r4, r2, lsl #1
    orr r4, r4, #1
    mov r8, r2, lsr #31

    @ Compare 64-bit remainder with trial
    cmp r5, r8
    blo sqrt_skip
    bhi subtract_trial

    cmp r3, r4
    blo sqrt_skip

subtract_trial:
    @ Subtract 64-bit trial divisor
    subs r3, r3, r4
    sbc r5, r5, r8

    @ Increment result
    add r2, r2, #1

sqrt_skip:
    subs r6, r6, #1
    bne sqrt_loop

    @ Return Q16.16 result
    mov r0, r2

    pop {r4-r10, pc}

