/* Attempting to output loop counter with printf */

        .global main
        .func main

main:
        PUSH {LR}
        BAL _loop

_loop:
        LDR R0, =string         @ seed printf
        LDR R1, =iterations
        LDR R1, [R1]            @ seed printf
        BL printf

        LDR R1, =iterations
        LDR R1, [R1]
        SUBS R1, #0x1
        LDR R2, =iterations
        STR R1, [R2]            @ write back

        BEQ _exit
        BNE _loop

_exit:
        POP {PC}
        MOV PC, LR

.data
string:
        .asciz "%d\n"
iterations:
        .word 5                 @ remaining loop iterations