.text
.global sqrtWithASM

sqrtWithASM:

    vsqrt.f32 s0, s0
    bx lr