
.data
.align 2
current_step:      .word 0
edge_count:        .word 0

full_step_table:     .byte 0b1010, 0b1001, 0b0101, 0b0110
half_step_table:     .byte 0b1000, 0b1010, 0b0010, 0b0110, 0b0100, 0b0101, 0b0001, 0b1001

.text
.global enable_routine
.global step_routine
.global step_timing_loop
.global get_timer_frequency
.global increment_edge_count
.global get_and_reset_count
.global get_current_timer
.extern E4235_Write

enable_routine:
    push {r4, lr}
    bl E4235_Write
    pop {r4, pc}

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

get_current_timer:
    mrrc p15, 1, r0, r1, c14
    bx lr
