.data
    one_exp: .float 1.0

    .text
    .global exp_n

exp_n:
    ldr r1, =one_exp
    vldr s1, [r1]             @ s1 = 1.0 (accumulator)

exponent_loop:
    cmp r0, #0
    beq done_exp
    vmul.f32 s1, s1, s0       @ result *= x
    sub r0, r0, #1            @ decrement n
    b exponent_loop

done_exp:
    vmov.f32 s0, s1           @ Move final result to s0 for C to read
    bx lr                     @ Return to C program