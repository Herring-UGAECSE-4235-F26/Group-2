@This is a delay program.  Your RP4 either runs at 1.5 or 1.8GHZ.  Using the program determine what speed your PI appears to be running
@Deliverable 1:  Calculate your RP4 clock speed.  Show how.
@Now let's make a stopwatch.   Use RP9:RP8:RP7 for minutes:seconds:hundredths of seconds.  Output the free running time to the terminal.  At 2:00:00, the stopwatch should go back to 0:00:00 and keep running.  The display
@output should be as described.
@You should look at the printloop.s example for the use of the printf command.  You will probably need to investigate formating using the asciz data type.
@Deliverable 2: your code on github and in your writeup.  We will check in class and look at accuracy as well.
	.data
	.global nanoCounter

	.global hundredthsSec
	.global seconds
	.global minutes

nanoCounter:	@nanosecond counter
	.word 10000000	@loads origional value of 1000000000 into the counter
hundredthsSec:
	.word 0
seconds:
	.word 0
minutes:
	.word 0

	.text
	.extern printCount
	.global main

main: 
	ldr r7, =hundredthsSec	@ load address of hundredthsSec into r7
	ldr	r7, [r7] @ loads value of address into r7
	ldr r8, =seconds	@ load address of seconds into r8
	ldr r8, [r8] @ loads value of address into r8
	ldr r9, =minutes	@ load address of minutes into r9
	ldr r9, [r9] @ loads value of address into r9
l1:	ldr r6, =nanoCounter	@ load address of nanoCounter into r1
	ldr r6, [r6] @load value of address into r1
	BL printCount
l2:	subs	r1, r1, #1		@ r1 = r1 – 1, decrement r1 (inner loop) 
	bne	l2			@ repeat it until r1 = 0 
	subs	r2, r2, #1		@ r2 = r2 – 1, decrement r2 (outer loop)
	ldr r3, =secondLevelCounter	@ load address of secondLevelCounter into r3
	str r2, [r3]	@ store value of r2 into value of secondLevelCounter mem location
	bne	l1			@ repeat it until r2 = 0

	mov r7, #1
	svc 0
