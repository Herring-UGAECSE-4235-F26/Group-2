
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
	@ save r0, r1, and r2 on stack before they are used using push 
	push {r0-r2}

	@ -------- modify r0, r1, and r2 
	mov  	r0, #0	@ r0 = 0
	mov  	r1, #0	@ r1 = 0
	mov  	r2, #0	@ r2 = 0
	@ --------

	@ restore the original registers contents from stack using pull
	pop {r0 - r2}

	bx	lr		 @ return to caller
