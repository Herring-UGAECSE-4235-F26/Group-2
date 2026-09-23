@ Group assignment
@ This program calculates the area of a circle
@ use gdb info all-registers  or i all-r
@ Change the code to print the results to the monitor in correct format.
@ Deliverables: 
@ 1) Provide a screen shot showing your program's output to the monitor in correct format for two values of radius: a) 0.375 and b) 5.125
@ 2) Copy and paste your final code into your report (yes, I normally don't have you do this, but it is small enough).  Comment the code.


	.text
	.global _start
    .extern printf
    .extern exit
_start:
	vmov.f32	s0,#0.125
	vmul.f32 	s0, s0, s0
	ldr	r2,=piNumber
	vldr		s1, [r2]
	vmul.f32	s0, s0, s1
	
here:	b	here

piNumber: .float 3.141593
radius: .float 0.375
output: .asciz "Area of the circle = %.6f\n"