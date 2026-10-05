@ Group assignment
@ This program calculates the area of a circle
@ use gdb info all-registers  or i all-r
@ Change the code to print the results to the monitor in correct format.
@ Deliverables: 
@ 1) Provide a screen shot showing your program's output to the monitor in correct format for two values of radius: a) 0.375 and b) 5.125
@ 2) Copy and paste your final code into your report (yes, I normally don't have you do this, but it is small enough).  Comment the code.
	.data
piNumber: .float 3.141593
radius_1: .float 0.375
radius_2: .float 5.125
output: .asciz "Area of the circle = %.6f\n"

	.text
	.global main
    .extern printf
main:
@loading pi to floating point register
	ldr	r2,=piNumber	@r2 = value of piNumber
	vldr s2, [r2]		@load r2 into s2
@area 1
	ldr r1,=radius_1	@r1=0.375
	vldr.f32	s1, [r1]	@load r1 into s1
	vmul.f32 	s0, s1, s1	@s0=s1*s1 
	vmul.f32	s0, s0, s2	@s0=s0*pi
	bl printOutput
@area 2
	ldr r1,=radius_2	@r1=5.125
	vldr.f32	s1, [r1]	@load r1 into s1
	vmul.f32 	s0, s1, s1	@s0=s1*s1
	vmul.f32	s0, s0, s2	@s0=s0*pi
	bl printOutput

	mov r7, #1
	svc 0
	
printOutput:
	push {r2, lr}
	ldr	r0,=output
	vcvt.f64.f32	d0, s0		@convert s0 to double precision
	vmov r2, r3, d0		@move d0 to r2 and r3 for printf
	bl	printf
	pop {r2, pc}		@restore r2
