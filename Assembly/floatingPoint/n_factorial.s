    .data

    factorialInput: .asciz "Calculate n!. Enter n: "
    factorialOutput: .asciz "n!= %d\n"
    input_format: .asciz "%d"

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
    cmp r0, #1

    mov r1, r4  @counter
    mov r2, #1  @r2 = result, initially 1

factorial_loop:
    cmp r1, #1
    ble done
    mul r2, r1, r2  @result = counter * result
    sub r1, r1, #1 @decrementing counter
    b factorial_loop

done:
    ldr r0, =factorialOutput
    mov r1, r4
    bl printf
    mov r0, #0
    pop {r4, pc}