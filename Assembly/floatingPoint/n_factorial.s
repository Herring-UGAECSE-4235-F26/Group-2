    .data

    factorialInput: .asciz "Calculate n!. Enter n: "
    factorialOutput: .asciz "%d! = %f\n"
    input_format: .asciz "%d"

    .align 2
    one: .float 1.0

.align 2
n: .word 0

.text
.global main
main:
    push {r4, lr}       @save r4 and return address

    @read n 
    ldr r0, =factorialInput
    bl printf
    ldr r0, =input_format
    ldr r1, =n
    bl scanf
    ldr r1, =n
    ldr r1, [r1]
    vmov s0, r1
    vcvt.f32.s32 s0, s0
    ldr r1, =one
    vldr s2, [r1]
    vmov s1, s2

factorial_loop:
    vcmp.f32 s0, s2
    vmrs APSR_nzcv, FPSCR
    ble done
    vmul.f32 s1, s1, s0
    vsub.f32 s0, s0, s2
    b factorial_loop

done:
    vcvt.f64.f32 d0, s1
    vmov r2, r3, d0
    ldr r1, =n
    ldr r1, [r1]
    ldr r0, =factorialOutput
    bl printf
    mov r0, #0
    pop {r4, pc}