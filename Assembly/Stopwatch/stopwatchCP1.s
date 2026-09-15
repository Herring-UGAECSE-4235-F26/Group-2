    .data

@ formatted string output for clock output
formatString:
        .asciz "\n%02d:%02d:%02d\r"

@ prog loop counter for nanoseconds
timingLoop:
    .word 7393000

    .text
    .global main

@ stores 0s into all clock components
main:
    mov r9, #0    @ minutes
    mov r8, #0    @ seconds
    mov r7, #0    @ hundredths of a second

@ prints out the clock values using printf. printf uses 4 arguments, r0-3, for print
printClock:
    ldr r0, =formatString   @first arg is formatted string
    mov r1, r9    @second arg is value of minutes
    mov r2, r8    @third arg is value of seconds
    mov r3, r7    @fourth arg is value of hundredthsSec
    bl printf   @ branches out to print

@flushes print output buffer
    ldr r0, =stdout   @points r0 to location of output buffer
    ldr r0, [r0]    @ r0 is set to contents of the buffer
    bl fflush    @buffer is cleared

@ initiates program nanosecond counter loop with default value in var timingLoop and prints time
initNanoCount:
    ldr r6, =timingLoop @ loads r6 with the address of timingLoop
    ldr r6, [r6] @ loads the value at timingLoop into r6

@ decrements nanocounter value. roughly = 1/100 of a second when done
nanoLoop:
    subs r6, r6, #1 @ decrement r6
    bne nanoLoop @ if r6 != 0, branch to nanoLoop

@ updates hundreths of a second value
updateTime:
    add r7, r7, #1  @adds 1 to hundredths of a second
    cmp r7, #100    @compares hundredths of a second to 100
    blt printClock    @ if hundredths of a second < 100, branch to initNanoCount
    add r8, r8, #1  @adds 1 to seconds
    cmp r8, #60  @compares seconds to 60
    blt main+8  @ if less than, jumps to main instruction set and adds 8 to the PC 
    mov r8, #0  @ resets seconds
    add r9, r9, #1  @adds 1 to minutes
    cmp r9, #2  @compares minutes to 2
    blt main+4  @ if less than, jumps to main instruction set and adds 4 to the PC
    bl main @ if equal to two, branches to top of main and resets everything.

    mov r7, #1
    svc 0