.text
.global fixed_point_sqrt
@initializing result and remainder to 0
fixed_point_sqrt:
    push {r4-r8, lr}
    mov r2, #0  @result = 0
    mov r3, #0  @ remainder = 0 lower 32 bits
    mov r5, #0  @remainder upper 32 bits
    mov r6, #32 @loopcounter (32 iterations for 64 bit)

sqrt_loop:
    @extracting next 2 bits from upper input
    lsr r4, r1, #30

    @Shifting 54 bit remainder left by 2 bits
    lsr r7, r3, #30
    lsl r5, r5, #2
    orr r5, r5, r7
    lsl r3, r3, r4

    @shifting 64 bit input left by 2 bits
    lsr r7, r0, #30
    lsl r1, r1, #2
    orr r1, r1, r7
    lsl r0, r0, #2

    @result shift left by 1 bit
    lsl r2, r2, #1

    @Calculate trial divisor: (result << 1) + 1
    lsl r4, r2, #1
    add r4, r4, #1

    @compare remainder with trial divisor
    cmp r5, #0
    bne subtract_trial

    cmp r3, r4
    blo sqrt_skip

subtract_trial:
    @if remainder >= trial divisor
    subs r3, r3, r4
    sbc r5, r5, #0
    add r2, r2, #1

sqrt_skip:
    @Decrement loop counter
    subs r6, r6, #1
    bne sqrt_loop

    @return sqrt in r0
    mov r0, r2
    pop {r4-r8, pc}
