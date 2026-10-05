.data
    one: .float 1.0

    .text
    .global n_factorial

n_factorial:
    vmov s0, r0               @ Move n to float register
    vcvt.f32.s32 s0, s0       @ Convert integer n to float
    ldr r1, =one
    vldr s2, [r1]             @ s2 = 1.0 (decrementer)
    vmov.f32 s1, s2           @ s1 = 1.0 (accumulator)

factorial_loop:
    vcmp.f32 s0, s2
    vmrs APSR_nzcv, FPSCR
    ble done_factorial
    vmul.f32 s1, s1, s0       @ result *= counter
    vsub.f32 s0, s0, s2       @ counter -= 1.0
    b factorial_loop

done_factorial:
    vmov.f32 s0, s1           @ Move final result to s0 for C to read
    bx lr                     @ Return to C program