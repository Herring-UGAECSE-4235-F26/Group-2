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
@ Turns the H-bridge on or off through the 4N25 optocoupler.
@ -------------------------------------------------------------------------
enable_routine:
    @ Push an even number of registers (r4, lr) to maintain 8-byte AAPCS stack alignment[cite: 9]
    push {r4, lr}          
    bl E4235_Write
    pop {r4, pc}

@ -------------------------------------------------------------------------
@ step_routine
@ Looks up the next coil pattern and drives the SN754410NE inputs.
@ -------------------------------------------------------------------------
step_routine:
    @ Push 6 registers (24 bytes) to maintain 8-byte AAPCS stack alignment[cite: 9]
    push {r4-r8, lr}       
    
    mov r4, r0             @ Save direction
    mov r5, r1             @ Save mode
    mov r6, r2             @ Save pin array pointer
    
    @ Load current step index
    ldr r7, =current_step
    ldr r8, [r7]

    @ Update step index based on direction
    cmp r4, #1
    addeq r8, r8, #1       
    subne r8, r8, #1       

    @ Handle wrapping based on mode
    cmp r5, #1
    beq half_step_wrap
    
full_step_wrap:
    and r8, r8, #3         
    ldr r4, =full_step_table
    b fetch_pattern
    
half_step_wrap:
    and r8, r8, #7         
    ldr r4, =half_step_table

fetch_pattern:
    @ Store updated step index
    str r8, [r7]
    
    @ Fetch the current bit pattern from the table
    ldrb r4, [r4, r8]      
    
    @ Drive Pin A (Bit 3)
    ldr r0, [r6, #0]       
    and r1, r4, #0b1000
    lsr r1, r1, #3         
    bl E4235_Write

    @ Drive Pin B (Bit 2)
    ldr r0, [r6, #4]       
    and r1, r4, #0b0100
    lsr r1, r1, #2
    bl E4235_Write

    @ Drive Pin C (Bit 1)
    ldr r0, [r6, #8]       
    and r1, r4, #0b0010
    lsr r1, r1, #1
    bl E4235_Write

    @ Drive Pin D (Bit 0)
    ldr r0, [r6, #12]      
    and r1, r4, #0b0001
    bl E4235_Write

    pop {r4-r8, pc}

@ -------------------------------------------------------------------------
@ step_timing_loop
@ Waits until the next step is due using the ARM generic timer[cite: 8, 9].
@ -------------------------------------------------------------------------
step_timing_loop:
    @ Push 4 registers (16 bytes) to maintain 8-byte AAPCS stack alignment[cite: 9]
    push {r4, r5, r6, lr}  
    mov r4, r0             @ Save target delay in ticks
    
    @ Read the 64-bit CNTPCT (Physical Count) into r5(lower) and r6(upper)
    mrrc p15, 0, r5, r6, c14 

wait_loop:
    @ Read the 64-bit CNTPCT into r2(lower) and r3(upper)
    mrrc p15, 0, r2, r3, c14 
    
    @ Calculate elapsed time (Current Lower - Start Lower)
    sub r2, r2, r5
    
    @ Compare elapsed time against target delay
    cmp r2, r4
    
    @ blo (Branch if Lower) performs an UNSIGNED comparison, immune to wraps
    blo wait_loop

    pop {r4, r5, r6, pc}
