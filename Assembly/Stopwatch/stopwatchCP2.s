     .data

@ Removing the terminal echo that's messing with the Stopwatch when it recieves an input
disableEcho:
	.asciz "stty -echo"
@ formatted string output for clock output
formatString:
        .asciz "%02d:%02d:%02d\r"

@ formatting key input as string
inputFormat:
    .asciz "%c"

@ Holds the keyboard input
inputChar:
    .byte 0

@ Creating state variables
.equ STATE_CLEAR, 0
.equ STATE_RUN, 1
.equ STATE_LAP, 2
.equ STATE_STOP, 3

@ prog loop counter for nanoseconds
timingLoop:
    .word 7393000

    .text
    .global main

@ stores 0s into all clock components
main:
    mov r0, #1	  @1 = deblocking ON
    bl E4235_KYBdeblock

    ldr r0, =disableEcho
    bl system

    @initializes stopwatch values to zero
    mov r9, #0    @ minutes
    mov r8, #0    @ seconds
    mov r7, #0    @ hundredths of a second
   
    @Start in CLEAR
    b clearState

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
    bl fflush

    b initNanoCount


@Prints the current stopwatch time without starting the timer
printOnly:
    push {lr}

    ldr r0, =formatString  @first arg is formatted string
    mov r1, r9             @second arg is value of minutes
    mov r2, r8             @third arg is value of seconds
    mov r3, r7             @fourth arg is value is value of hundredthsSec
    bl printf              @branches to print

    pop {lr}
    bx lr

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
    blt checkRunKey    @ if hundredths of a second < 100, checks for input
    
    mov r7, #0
    add r8, r8, #1  @adds 1 to seconds
    cmp r8, #60  @compares seconds to 60
    blt checkRunKey @if less than 60, checks for input
    mov r8, #0  @ resets seconds
    add r9, r9, #1  @adds 1 to minutes
    cmp r9, #60  @compares minutes to 60
    blt checkRunKey
    
    @mov r7, #1
    @svc 0
    b clearState

@ When C is pressed, enters this state
clearState:
    mov r9, #0
    mov r8, #0
    mov r7, #0
    
    bl printOnly


@ While in clearState, waits for R to update the state 
clearWait:
    bl checkKey

    cmp r10, #'R' @Checks for R key
    beq runState  @Starts stopwatch if R is pressed

    b clearWait

@Checks for keyboard input while stopwatch is running
checkRunKey:
    bl checkKey

    @if L is pressed, go to lapState
    cmp r10, #'L'
    beq lapState

    @if C is pressed go to clearState
    cmp r10, #'C'
    beq clearState

    @if S is pressed, go to stopState
    cmp r10, #'S'
    beq stopState

    @ No input, Continue time
    b printClock

@Starts or resumes normal stopwatch 
runState:
    b initNanoCount

@Timing loop used when displaying a lap time
lapNanoCount:
    ldr r6, =timingLoop
    ldr r6, [r6]

@Creates the timing delay for LAP mode
lapNanoLoop:
    subs r6, r6, #1
    bne lapNanoLoop

    add r7, r7, #1
    cmp r7, #100
    blt lapState

    mov r7, #0
    add r8, r8, #1
    cmp r8, #60
    blt lapState

    mov r8, #0
    add r9, r9, #1
    cmp r9, #60
    blt lapState

    b clearState

@Handles keyboard input while in LAP mode
lapState:
    bl checkKey

    @if R is pressed, go to runState
    cmp r10, #'R'
    beq lapRun

    @if L is pressed, go to lapState, does nothing
    cmp r10, #'L'
    beq lapState

    @if C is pressed go to clearState
    cmp r10, #'C'
    beq clearState

    @if S is pressed, go to stopState
    cmp r10, #'S'
    beq stopState

    @ No input, Continue time
    b lapNanoCount

@ Displays the lap time and resumes normal timing
lapRun:
    bl printClock
    b initNanoCount

@Displays the current time and waits for input, timing ends
stopState:
    bl printOnly

@Waits for R and C while stopwatch is waiting for input, L does nothing
stopWait:
    bl checkKey

    cmp r10, #'R'
    beq runState

    cmp r10, #'C'
    beq clearState

    cmp r10, #'L'
    beq stopWait

    b stopWait

@Reads a keyboard character and stores it in r10
checkKey:
    push {lr}
    bl getchar
 
    mov r10, r0
    @ldr r1, =inputChar
    @ldrb r10, [r1]

    pop {lr}
    bx lr
