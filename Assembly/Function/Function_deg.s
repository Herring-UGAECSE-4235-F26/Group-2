.data


prompt:
	.asciz "Enter Angle:"

input:
	.asciz "%d"

@handles angles 4 deg - 88 deg
output:
	.asciz "Cosine of %d = 0.%03d and Sine of %d = 0.%03d\n"

output0:
	.asciz "Cosine of 0 = 1.000 and Sine of 0 = 0.000\n"

output90:
	.asciz "Cosine of 90 = 0.000 and Sine of 90 = 1.000\n"

angle:
	.word 0
.align 2
Sine_Table:
	.word 0, 70, 139, 208, 276, 342, 407, 469, 530, 588, 643, 695, 743, 788, 829, 866, 899, 927, 951, 970, 985, 995, 999, 1000

.align 2
Cosine_Table:
	.word 1000, 998, 990, 978, 961, 940, 914, 883, 848, 809, 766, 719, 669, 616, 559, 500, 438, 375, 309, 242, 174, 105, 35, 0

.text
.global main
.extern printf
.extern scanf

main:
	push {r4-r10, lr}

inputLoop:
	@step 1: display enter angle
	ldr r0, =prompt
	bl printf

	@Reading integer from the keyboard
	ldr r0, =input
	ldr r1, =angle
	bl scanf

	@store entered angle in r9
	ldr r2, =angle
	ldr r9, [r2]

	@Comparing input to zero and 90 to ensure input within range
	cmp r9, #0
	blt inputLoop
	cmp r9, #90
	bgt inputLoop

	cmp r9, #90
	beq NinetyValue

	tst r9, #3	@performs an AND bitwise operation to see if input is divisible by 4
	bne inputLoop
	
	@finding table index for angle
	mov r4, r9
	b loadResult

NinetyValue:
	mov r4,#23 @table index for 90
loadResult:
	ldr r5, =Sine_Table
	ldr r8, [r5,r4]

	ldr r5, =Cosine_Table
	ldr r10, [r5,r4]

	mov r0, r8
	mov r1, r10

	cmp r9, #0
	beq print0

	cmp r9, #90
	beq print90

printRegular:
	sub sp, sp, #8
	str r8, [sp]

	ldr r0, =output
	mov r1, r9
	mov r2, r10
	mov r3, r9
	bl printf
	add sp, sp, #8
	b restore

print0:
	ldr r0, =output0
	bl printf
	b restore
print90:
	ldr r0, =output90
	bl printf
	b restore

restore:
	mov r0, r8
	mov r1, r10
	b inputLoop
