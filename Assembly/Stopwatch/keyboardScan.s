.global _start

.equ STDIN,      0
.equ STDOUT,     1
.equ SYS_READ,   3
.equ SYS_WRITE,  4
.equ SYS_IOCTL,  54
.equ SYS_EXIT,   1
.equ FIONREAD,   0x541B     @ ioctl flag to get number of bytes in stdin buffer

.section .text
_start:
    @ Print the initialization message
    mov r0, #STDOUT
    ldr r1, =msg_start
    mov r2, #msg_len
    mov r7, #SYS_WRITE
    svc #0

scan_loop:
    @ 1. Check if there are any characters waiting in the stdin buffer
    mov r0, #STDIN
    mov r1, #FIONREAD
    ldr r2, =bytes_avail    @ Pointer to hold the byte count
    mov r7, #SYS_IOCTL
    svc #0

    @ 2. Load the number of bytes available
    ldr r1, =bytes_avail
    ldr r2, [r1]
    cmp r2, #0
    beq no_input            @ If 0 bytes, jump to loop/idle logic

    @ 3. If a character exists, read it
    mov r0, #STDIN
    ldr r1, =char_buf       @ Destination buffer
    mov r2, #1              @ Read 1 byte
    mov r7, #SYS_READ
    svc #0

    @ 4. Check if the pressed key is 'q' to exit
    ldr r1, =char_buf
    ldrb r0, [r1]
    cmp r0, #'q' //Exit on 'q'
    beq exit

    @ Accept l
	cmp r0, #'l' //key for lap count
	beq valid_key
    @ Accept s
	cmp r0, #'s' //key for stop
	beq valid_key
    @ Accept c
	cmp r0, #'c' //key for clear
	beq valid_key
    @ Accept r
	cmp r0, #'r' //key for Run
	beq valid_key

    @Any other key is ignored
	b scan_loop
valid_key:
    @ 5. Otherwise, echo the scanned key back to the user
    mov r0, #STDOUT
    ldr r1, =msg_key
    mov r2, #msg_k_len
    mov r7, #SYS_WRITE
    svc #0

    mov r0, #STDOUT
    ldr r1, =char_buf
    mov r2, #1
    mov r7, #SYS_WRITE
    svc #0

    b scan_loop
no_input:
    @ Simulate doing other processing work inside your application loop
    @ (Optional: insert a small system sleep syscall here to keep CPU utilization down)
    b scan_loop

exit:
    @ Exit program cleanly
    mov r0, #0
    mov r7, #SYS_EXIT
    svc #0

.section .data
msg_start:  .ascii "Scanning... Press keys to echo. Press 'q' to quit.\n"
msg_len = . - msg_start

msg_key:    .ascii "\nKey Scanned: "
msg_k_len = . - msg_key

.section .bss
.align 2
bytes_avail: .word 0        @ Allocation for ioctl byte counter
char_buf:    .space 1       @ 1-byte buffer for the scanned key
