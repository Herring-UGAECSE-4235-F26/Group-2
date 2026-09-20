
	.text
	.global _start 
_start:
	ldr  	r0, =0x125		@ r0 = 0x125
	ldr  	r1, =0x144		@ r1 = 0x144
	mov  	r2, #0x56		@ r2 = 0x56
	bl	myFunc			@ call a subroutine
	add	r3, r0, r1		@ r3 = r0 + r1 = 0x125 + 0x144 = 0x269
	add	r3, r3, r2		@ r3 = r3 + r2 = 0x269 + 0x56 = 0x2bf

	mov	r7, #1
	svc	0
	
myFunc:
	@ save r0, r1, and r2 on stack before they are used 
	sub  	r13, r13, #4 @ r13 = r13 - 4, to decrement the stack pointer
	str  	r0, [r13]	 @ save r0 on stack
	sub  	r13, r13, #4 @ r13 = r13 - 4, to decrement the stack pointer
	str  	r1, [r13]	 @ save r1 on stack
	sub  	r13, r13, #4 @ r13 = r13 - 4, to decrement the stack pointer
	str  	r2, [r13]	 @ save r2 on stack

	@ -------- modify r0, r1, and r2 
	mov  	r0, #0	@ r0 = 0
	mov  	r1, #0	@ r1 = 0
	mov  	r2, #0	@ r2 = 0
	@ --------

	@ restore the original registers contents from stack
	ldr	r2, [r13]	 @ restore r2 from stack
	add	r13, r13, #4 @ r13 = r13 + 4 to increment the stack pointer
	ldr	r1, [r13]	 @ restore r1 from stack
	add	r13, r13, #4 @ r13 = r13 + 4 to increment the stack pointer
	ldr	r0, [r13]	 @ restore r0 from stack
	add	r13, r13, #4 @ r13 = r13 + 4 to increment the stack pointer

	bx	lr		 @ return to caller