@ Constants for blink at GPIO22
.equ    GPFSEL2, 0x08   @ function register offset
.equ    GPCLR0, 0x28    @ clear register offset
.equ    GPSET0, 0x1c    @ set register offset

.equ    PIN, 22                         @ Target GPIO 22
.equ    GPFSEL2_GPIO22_MASK, (0b111 << 6)   @ Bits 6-8 for GPIO 22
.equ    MAKE_GPIO22_OUTPUT, (0b001 << 6)    @ 001 for Output

@ Args for mmap
.equ    BLOCK_SIZE, 4096        
.equ    MAP_SHARED, 1    
.equ    PROT_RDWR, 0x3   

    .section .rodata
device:
    .asciz  "/dev/gpiomem"

    .section .data
@ Increased to 150 million for a ~0.5 second delay
@ 50%50% 100Hz both on and off should be 3700000
@ 50%50% 1kHz both should be 370000
@ 75%25% on: 555000; off: 185000
On_time:  .word 3700000
Off_time: .word 3700000

    .text
    .global main
main:
    @ Preserve registers and allocate 8 bytes on stack for mmap args
    push    {r4, r5, lr}
    sub     sp, sp, #8

@ Open /dev/gpiomem 
    ldr     r1, O_RDWR_O_SYNC   
    ldr     r0, mem_fd          
    bl      open     
    mov     r4, r0              @ Save file descriptor in r4

@ Setup mmap(NULL, 4096, PROT_RDWR, MAP_SHARED, fd, 0)
    mov     r0, #0              @ 1st arg: NULL (Let OS choose address)
    mov     r1, #BLOCK_SIZE     @ 2nd arg: 4096
    mov     r2, #PROT_RDWR      @ 3rd arg: Read/Write
    mov     r3, #MAP_SHARED     @ 4th arg: Shared
    
    str     r4, [sp, #0]        @ 5th arg: fd (saved at sp + 0)
    mov     r6, #0              
    str     r6, [sp, #4]        @ 6th arg: offset 0 (saved at sp + 4)
    bl      mmap
    mov     r5, r0              @ Save the virtual memory address in r5

@ Set up the GPIO pin function register
    add     r0, r5, #GPFSEL2            
    ldr     r2, [r0]                    
    bic     r2, r2, #GPFSEL2_GPIO22_MASK @ clear pin field
    orr     r2, r2, #MAKE_GPIO22_OUTPUT  @ enter function code
    str     r2, [r0]                    

loop:
@ Turn on
    add     r0, r5, #GPSET0 
    mov     r3, #1          
    lsl     r3, r3, #PIN    
    str     r3, [r0]        

    ldr     r1, =On_time
    ldr     r1, [r1]
    bl      delay_loop

@ Turn off
    add     r0, r5, #GPCLR0 
    mov     r3, #1          
    lsl     r3, r3, #PIN    
    str     r3, [r0]        

    ldr     r1, =Off_time
    ldr     r1, [r1]
    bl      delay_loop

    b       loop

@ Delay Subroutine 
delay_loop:
    subs    r1, r1, #1
    bne     delay_loop
    bx      lr

mem_fd:
    .word   device
O_RDWR_O_SYNC:
    .word   2|256       @ O_RDWR (2)|O_SYNC (256)
