@ ============================================================================
@ motor_driver.s  -  PARMCO H-bridge interface (ARM32, AAPCS)
@
@ AI-generated (CP1/CP2 routines by the team's earlier AI sessions; CP3
@ additions step_at_deadline, set_step_index and get_step_index by Claude).
@ All GPIO access goes through the class library (E4235_Write).
@
@ Routines required by the project spec:
@   step_routine      - looks up the next coil pattern in a table for the given
@                       direction and full/half-step mode and drives the four
@                       SN754410NE inputs through E4235_Write.
@   enable_routine    - turns the H-bridge on/off through the 4N25 (E4235_Write).
@   step_at_deadline  - step-timing loop: waits on the ARM generic timer until
@                       the next step is due, then calls step_routine.
@ ============================================================================

.data
.align 2
current_step:      .word 0
edge_count:        .word 0

@ Bits correspond to driver inputs: bit3=1A, bit2=2A, bit1=3A, bit0=4A
full_step_table:     .byte 0b1010, 0b1001, 0b0101, 0b0110
half_step_table:     .byte 0b1000, 0b1010, 0b0010, 0b0110, 0b0100, 0b0101, 0b0001, 0b1001

.text
.global enable_routine
.global step_routine
.global step_timing_loop
.global step_at_deadline
.global set_step_index
.global get_step_index
.global get_timer_frequency
.global increment_edge_count
.global get_and_reset_count
.global get_current_timer
.extern E4235_Write

@ ---------------------------------------------------------------------------
@ void enable_routine(int pin, int state)
@ Drives the 4N25 LED (H-bridge enable) through the class library.
@ ---------------------------------------------------------------------------
enable_routine:
    push {r4, lr}
    bl E4235_Write
    pop {r4, pc}

@ ---------------------------------------------------------------------------
@ void step_routine(int direction, int mode, int *pins)
@ direction: 1 = index +1, 0 = index -1.  mode: 0 = full, 1 = half.
@ ---------------------------------------------------------------------------
step_routine:
    push {r4-r8, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    ldr r7, =current_step
    ldr r8, [r7]
    cmp r4, #1
    addeq r8, r8, #1
    subne r8, r8, #1
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
    str r8, [r7]
    ldrb r4, [r4, r8]
    ldr r0, [r6, #0]
    and r1, r4, #0b1000
    lsr r1, r1, #3
    bl E4235_Write
    ldr r0, [r6, #4]
    and r1, r4, #0b0100
    lsr r1, r1, #2
    bl E4235_Write
    ldr r0, [r6, #8]
    and r1, r4, #0b0010
    lsr r1, r1, #1
    bl E4235_Write
    ldr r0, [r6, #12]
    and r1, r4, #0b0001
    bl E4235_Write
    pop {r4-r8, pc}

@ ---------------------------------------------------------------------------
@ void step_at_deadline(unsigned int deadline, int direction, int mode, int *pins)
@ CP3 step-timing loop. Busy-waits until the low 32 bits of the ARM generic
@ timer (CNTPCT) reach 'deadline', then calls step_routine. The comparison is
@ signed (now - deadline >= 0) so it is correct across the 32-bit wrap
@ (~79 s at 54 MHz). Using an absolute deadline means the time spent inside
@ step_routine does not add to the step period, so the step rate does not drift.
@ ---------------------------------------------------------------------------
step_at_deadline:
    push {r4-r8, lr}          @ 6 registers keeps the stack 8-byte aligned
    mov r4, r0                @ deadline
    mov r5, r1                @ direction
    mov r6, r2                @ mode
    mov r7, r3                @ pins
deadline_wait:
    mrrc p15, 1, r0, r1, c14  @ r0 = CNTPCT[31:0], r1 = CNTPCT[63:32]
    sub r0, r0, r4
    cmp r0, #0
    blt deadline_wait
    mov r0, r5
    mov r1, r6
    mov r2, r7
    bl step_routine
    pop {r4-r8, pc}

@ ---------------------------------------------------------------------------
@ void set_step_index(int index) / int get_step_index(void)
@ Lets C re-align the coil-pattern index with the encoder (feedback hold).
@ ---------------------------------------------------------------------------
set_step_index:
    ldr r1, =current_step
    str r0, [r1]
    bx lr

get_step_index:
    ldr r1, =current_step
    ldr r0, [r1]
    bx lr

@ ---------------------------------------------------------------------------
@ void step_timing_loop(unsigned int ticks)   (CP1/CP2 relative delay, kept)
@ ---------------------------------------------------------------------------
step_timing_loop:
    push {r4-r6, lr}
    mov r4, r0
    mrrc p15, 1, r5, r6, c14
wait_loop:
    mrrc p15, 1, r1, r2, c14
    sub r1, r1, r5
    cmp r1, r4
    blo wait_loop
    pop {r4-r6, pc}

@ unsigned int get_timer_frequency(void)  - CNTFRQ
get_timer_frequency:
    mrc p15, 0, r0, c14, c0, 0
    bx lr

increment_edge_count:
    ldr r0, =edge_count
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    bx lr

get_and_reset_count:
    ldr r1, =edge_count
    ldr r0, [r1]
    mov r2, #0
    str r2, [r1]
    bx lr

@ unsigned int get_current_timer(void)  - CNTPCT[31:0]
get_current_timer:
    mrrc p15, 1, r0, r1, c14
    bx lr
