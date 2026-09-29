@ THIS IS A GROUP PROJECT
@This is from the book "Modern Assembly Language Programming with the ARM Processor" 1st edition
@This is written as a subroutine call (note the last instruction).  You may need to make changes for a stand-alone program.
@This calculates sin(x) (assumes x is passed in S0.  x is a real number in radians) by summing the first six terms of the Taylor series.
@ Deliverables for writeup
@ 1) Write out the mathematical formula for the first six terms of the Taylor series to calculate sin(x).
@ 2) Change the program with a valid value for x in S0 and show this working with a photo of the output.
@ 3) Modify the code to add a keyboard input for the x value to load into s0 with result to monitor in valid format.
@ We will check this in class. (see check sheet on ELC).  We want to enter x in degrees so you will need to convert this to radians for the Tayler series.
@ As always, make sure there is a link to your final github code in your writeup.
@We will check code on Github.  

.data
prompt: .asciz "Enter x in degrees: "
input:  .asciz "%f"
output: .asciz "sin(%.2f degrees) = %.6f\n"
error:  .asciz "Please enter a number"

.align 2
degrees:  .float 0.0
deg_to_rad: .float .01745329252     @pi / 180

.align 6 @ Align to cache
ctab: .word 0xBE2AAAAB @ -1.666667e-01
.word 0x3C088889 @ 8.333334e-03
.word 0xB9500D01 @ -1.984127e-04
.word 0x3638EF1D @ 2.755732e-06
.word 0xB2D7322B @ -2.505211e-08

.global main
main:
    push {r4, lr}
    sub sp, sp, #8

    ldr r0, =prompt
    bl printf

    ldr r0, =input
    ldr r1, =degrees
    bl scanf
    cmp r0, #1
    bne bad_input

    ldr r0, =degrees
    vldr s0, [r0]   @s0 = degrees
    ldr r0, =deg_to_rad
    vldr s1, [r0]
    vmul.f32 s0, s0, s1     @s0 = radians

    bl sin_v_f  @result is in s0

    vcvt.f64.f32 d0, s0
    vstr d0, [sp]   @result is printf's second number

    ldr r0, =degrees
    vldr s0, [r0]
    vcvt.f64.f32 d0, s0
    vmov r2, r3, d0 @degrees are printf's first number
    ldr r0, =output

    mov r0, #0
    b finish

bad_input:
    ldr r0, =error

finish:
    add sp, sp, #8
    pop {r4, pc}


@@@ ---------------------------------------------------------------
.text
.align 2
.global sin_v_f
sin_v_f:@@ set runfast mode and rounding to nearest
    vmrs r1, fpscr @ get FPSCR contents in r1
    bic r2, r1, #(0b1111<<23)
    orr r2, r2, #(0b1100<<23)
    vmsr fpscr, r2 @ store settings in FPSCR
    vmul.f32 s1,s0,s0 @ s1 = x^2

    ldr r0,=ctab @ get address of coefficients
    vldmia r0!,{s16-s20} @ load all coefficients into Bank 2
    vmul.f32 s8,s0,s1 @ s8 = x^3
    vmul.f32 s9,s8,s1 @ s9 = x^5
    vmul.f32 s10,s9,s1 @ s10 = x^7
    vmul.f32 s11,s10,s1 @ s11 = x^9
    vmul.f32 s12,s11,s1 @ s12 = x^11
    
    @@ Set VFP for vector mode
    bic r2, r2, #(0b11111<<16) @ set rounding, stride to 1,
    orr r2, r2, #(0b00100<<16) @ and vector length to 5
    vmsr fpscr, r2 @ store settings in FPSCR
    vmul.f32 s24,s8,s16 @ VECTOR operation x^(2n+1) * coeff[n]
    vmsr fpscr, r1 @ restore original FPSCR

    @@ Add terms in Bank 3 to the result in s0
    vadd.f32 s24,s24,s25
    vadd.f32 s26,s26,s27
    vadd.f32 s0,s0,s24
    vadd.f32 s26,s26,s28
    vadd.f32 s0,s0,s26
    mov pc,lr		@can also use bx lr