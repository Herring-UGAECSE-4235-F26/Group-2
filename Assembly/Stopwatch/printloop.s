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
        LDR R0, =indent         @prints stored string indent
        LDR R1, =minutes      @r1 points to minutes
        LDR R1, [R1]            @r1 is set to the value of minutes
        BL printf
        ldr r0, =colon         @prints stored string colon
        LDR R1, =seconds        @loads address of seconds
        LDR R1, [R1]    @stores value of seconds
        BL printf
        ldr r0, =colon         @prints stored string colon
        LDR R1, =hundredthsSec        @loads address of hundredthsSec
        LDR R1, [R1]    @stores value of hundredthsSec
        BL printf
        BAL _exit

_exit:
        POP {PC}
        MOV PC, LR

.data
indent:
        .asciz "%d\n"
colon:
        .asciz ":"
