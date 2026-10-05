    .data
    
    exponentXInput: .asciz "Calculate x^n. Enter x: "
    exponentNInput: .asciz "Enter n: "
    exponentOutput: .asciz "%f^%d= %f\n"
    float_format: .asciz "%f"
    int_format:     .asciz "%d"

    one: .float 1.0


.align 2
x: .float 0.0
n: .word 0

.text
.global main
main:
    push {r4, lr} @save return address

    @read x
    ldr r0, =exponentXInput
    bl printf

    ldr r0, =float_format
    ldr r1, =x
    bl scanf
    cmp r0, #1


    @ Read n 
    ldr r0, =exponentNInput
    bl printf

    ldr r0, =int_format
    ldr r1, =n 
    bl scanf
    cmp r0, #1


    ldr r1, =x
    vldr s0, [r1]   @s0 = x
    ldr r1, =n
    ldr r0, [r1]    @r0 = n 
    cmp r0, #0

    ldr r1, =one
    vldr s2, [r1]
    vmov.f32 s1, s2

exponent_loop:
    cmp r0, #0
    beq done
    vmul.f32 s1, s1, s0 @result*x
    sub r0, r0, #1      @decreasing count
    b exponent_loop

done:
    vcvt.f64.f32 d0, s0
    vcvt.f64.f32 d1, s1
    vpush {d1}
    ldr r2, =n
    ldr r2, [r2]
    mov r3, #0
    push {r2, r3}
    vmov r2, r3, d0
    ldr r0, =exponentOutput
    bl printf
    add sp, sp, #16
    mov r0, #0
    pop {r4, pc}