.data
.align 2

@ Tracks the current position in the step sequence (0-7)
current_step: 
    .word 0

@ Full-step coil patterns (4 steps: AB, BC, CD, DA)
@ Bits correspond to coils: 3=A, 2=B, 1=C, 0=D
full_step_table:
    .byte 0b1010, 0b0110, 0b0101, 0b1001

@ Half-step coil patterns (8 steps)
half_step_table:
    .byte 0b1000, 0b1010, 0b0010, 0b0110, 0b0100, 0b0101, 0b0001, 0b1001

.text
.global enable_routine
.global step_routine
.global step_timing_loop
.extern E4235_Write

@ -------------------------------------------------------------------------
@ enable_routine
@ Turns the H-bridge on or off through the 4N25 optocoupler[cite: 5, 8].
@ Args:
@   r0: Enable pin number
@   r1: State (1 = Enable, 0 = Disable)
@ -------------------------------------------------------------------------
enable_routine:
    push {lr}
    @ AAPCS: r0 (GPIO) and r1 (state) are passed directly to the library[cite: 46, 51]
    bl E4235_Write
    pop {pc}

@ -------------------------------------------------------------------------
@ step_routine
@ Looks up the next coil pattern and drives the SN754410NE inputs.
@ Args:
@   r0: Direction (1 = CW, 0 = CCW)
@   r1: Mode (1 = Half step, 0 = Full step)
@   r2: Pointer to integer array containing the 4 motor pins {A, B, C, D}
@ -------------------------------------------------------------------------
step_routine:
    push {r4-r8, lr}
    
    mov r4, r0             @ Save direction
    mov r5, r1             @ Save mode
    mov r6, r2             @ Save pin array pointer pointer
    
    @ Load current step index
    ldr r7, =current_step
    ldr r8, [r7]

    @ Update step index based on direction
    cmp r4, #1
    addeq r8, r8, #1       @ If CW, increment
    subne r8, r8, #1       @ If CCW, decrement

    @ Handle wrapping based on mode
    cmp r5, #1
    beq half_step_wrap
    
full_step_wrap:
    and r8, r8, #3         @ Wrap 0-3 for full step (modulo 4)
    ldr r4, =full_step_table
    b fetch_pattern
    
half_step_wrap:
    and r8, r8, #7         @ Wrap 0-7 for half step (modulo 8)
    ldr r4, =half_step_table

fetch_pattern:
    @ Store updated step index
    str r8, [r7]
    
    @ Fetch the current bit pattern from the table
    ldrb r4, [r4, r8]      @ r4 now holds the 4-bit pattern
    
    @ Drive Pin A (Bit 3)
    ldr r0, [r6, #0]       @ Load Pin A number from array into r0[cite: 46, 51]
    and r1, r4, #0b1000
    lsr r1, r1, #3         @ Shift bit down to 0 or 1 for state in r1[cite: 46, 51]
    bl E4235_Write[cite: 46, 57]

    @ Drive Pin B (Bit 2)
    ldr r0, [r6, #4]       
    and r1, r4, #0b0100
    lsr r1, r1, #2
    bl E4235_Write[cite: 46, 57]

    @ Drive Pin C (Bit 1)
    ldr r0, [r6, #8]       
    and r1, r4, #0b0010
    lsr r1, r1, #1
    bl E4235_Write[cite: 46, 57]

    @ Drive Pin D (Bit 0)
    ldr r0, [r6, #12]      
    and r1, r4, #0b0001
    bl E4235_Write[cite: 46, 57]

    pop {r4-r8, pc}

@ -------------------------------------------------------------------------
@ step_timing_loop
@ Waits until the next step is due using the ARM generic timer[cite: 8, 9].
@ Args:
@   r0: Target wait interval in timer ticks
@ -------------------------------------------------------------------------
step_timing_loop:
    push {r4, lr}
    mov r4, r0             @ Save target interval
    
    @ Read current 32-bit timer value (CNTPCT lower half) into r1
    mrc p15, 0, r1, c14, c0, 0
    add r4, r1, r4         @ Calculate target end time

wait_loop:
    mrc p15, 0, r1, c14, c0, 0
    cmp r1, r4
    blt wait_loop          @ Keep looping until current time >= end time

    pop {r4, pc}