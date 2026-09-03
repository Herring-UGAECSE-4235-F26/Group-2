/* Attempting to output loop counter with printf */
        .text
        .global printCount

 /*      .extern hundredthsSec
        .extern seconds
        .extern minutes
*/
printCount:
        PUSH {LR}
        BAL _loop

_loop:
        ldr R0, =formatString       @prints stored string indent
        LDR R1, =minutes      @r1 points to minutes
        LDR R1, [R1]            @r1 is set to the value of minutes
        ldr R2, =seconds      @r2 points to seconds
        LDR R2, [R2]            @r2 is set to the value of seconds
        ldr R3, =hundredthsSec      @r3 points to hundredthsSec
        LDR R3, [R3]            @r3 is set to the value of hundredthsSec
        BL printf
        BAL _exit

_exit:
        POP {PC}
        MOV PC, LR

.data
formatString:
        .asciz "%02d:%02d:%02d\n"
