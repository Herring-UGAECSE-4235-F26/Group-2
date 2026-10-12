.text
.global fixed_point_sqrt
@initializing result and remainder to 0
fixed_point_sqrt:
    mov r1, #0  @result = 0
    mov r2, #0  @ remainder = 0
    mov r3, #16 @loopcounter (16 iterations for 32 bit)

sqrt_loop:
    @shifting remainder by 2 bits
    lsl r2, r2, #2
    @extracting next 2 bits from r0
    lsr r4, r0, #30
    orr r2, r2, r4
    lsl r0, r0, #2

    @result shift left by 1 bit
    lsl r1, r1, #1

    @Calculate trial divisor: (result << 1) + 1
    lsl r4, r1, #1
    add r4, r4, #1

    @compare remainder with trial divisor
    cmp r2, r4
    blt sqrt_skip

    @if remainder >= trial divisor
    sub r2, r2, r4
    add r1, r1, #1

sqrt_skip:
    @Decrement loop counter
    subs r3, r3, #1
    bne sqrt_loop

    @return sqrt in r0
    mov r0, r1
    bx lr