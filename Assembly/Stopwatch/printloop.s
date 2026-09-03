/* Attempting to output loop counter with printf */
        .text
        .global printCount

        .extern hundredthsSec
        .extern seconds
        .extern minutes

printCount:
        PUSH {LR}
        BAL _loop

_loop:
        LDR R0, =string         @ seed printf
        LDR R1, =hundredthsSec      @r1 points to hundredthsSec
        LDR R1, [R1]            @r1 is set to the value of hundredthsSec
        BL printf       @prints hundredthsSec value

        BAL _exit

_exit:
        POP {PC}
        MOV PC, LR

.data
string:
        .asciz "%d\n"
        