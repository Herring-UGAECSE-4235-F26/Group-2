.text
.global quake_sqrt

quake_sqrt:
    @Saving original input
    vmov.f32 s1, s0

    @Interpreting floating point as integer
    vmov r0, s0

    lsr r0, r0, #1 
    ldr r1, =0x5F3759DF   @magic constant from quake algorithm, gives a good approximation of 1/(x)^(1/2)
    sub r0, r1, r0

    vmov s0, r0

    @Calculate half of original output
    ldr r2, =half
    vldr s2, [r2]

    vmul.f32 s2, s1, s2

    @y = y*(1.5 -.5 * x * y * y)
    vmul.f32 s3, s0, s0
    vmul.f32 s3, s2, s3

    ldr r2, =oneHalf
    vldr s4, [r2]
    vsub.f32 s3, s4, s3

    vmul.f32 s0,s0, s3

    @multiply by original input to obtain sqrt
    vmul.f32 s0, s1, s0

    bx lr

.align 2
half: .float 0.5
oneHalf: .float 1.5
