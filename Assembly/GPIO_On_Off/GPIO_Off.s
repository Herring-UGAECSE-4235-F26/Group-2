@ mmap part taken from by https://bob.cs.sonoma.edu/IntroCompOrg-RPi/sec-gpio-mem.html

@ Constants for blink at GPIO21
@ GPFSEL2 [Offset: 0x08] responsible for GPIO Pins 20 to 29
@ GPCLR0 [Offset: 0x28] responsible for GPIO Pins 0 to 31
@ GPSET0 [Offest: 0x1C] responsible for GPIO Pins 0 to 31

@ GPIO21 Related
.equ    GPFSEL2, 0x08   @ function register offset
.equ    GPCLR0, 0x28    @ clear register offset
.equ    GPSET0, 0x1c    @ set register offset
.equ    GPFSEL2_GPIO20_MASK, 0b111   @ Mask for fn register @ changed froom 0b111000 to 0b111
.equ    MAKE_GPIO20_OUTPUT, 0b001      @ use pin for ouput @ changed from 0b1000 to 0b001
.equ    PIN, 22                         @ Used to set PIN high / low @ changed from 21 to 20

@ Args for mmap
.equ    OFFSET_FILE_DESCRP, 0   @ file descriptor
.equ    mem_fd_open, 3
.equ    BLOCK_SIZE, 4096        @ Raspbian memory page
.equ    ADDRESS_ARG, 3          @ device address

@ Times
.equ    On_time, 1000000000            @ input number in nanoseconds for amount of time in "on" state
.equ    Off_time, 1000000000           @ input number in nanoseconds for amount of time in "off" state

@ The following are defined in /usr/include/asm-generic/mman-common.h:
.equ    MAP_SHARED,1    @ share changes with other processes
.equ    PROT_RDWR,0x3   @ PROT_READ(0x1)|PROT_WRITE(0x2)

@ Constant program data
    .section .rodata
device:
    .asciz  "/dev/gpiomem"


@ The program
    .text
    .global main
main:
@ Open /dev/gpiomem for read/write and syncing
    ldr     r1, O_RDWR_O_SYNC   @ flags for accessing device
    ldr     r0, mem_fd          @ address of /dev/gpiomem
    bl      open     
    mov     r4, r0              @ use r4 for file descriptor


@ Turn off
    add     r0, r5, #GPCLR0 @ calc GPCLR0 address

    mov     r3, #1          @ turn off bit
    lsl     r3, r3, #PIN    @ shift bit to pin position
    orr     r2, r2, r3      @ set bit
    str     r2, [r0]        @ update register
    ldr     r8, =Off_time   @ r8 = Off_time
    udiv    r6, r8, r10     @ r6 = Off_time/10
    mul     r6, r6, r9      @ r6 = (Off_time/10) * 9
