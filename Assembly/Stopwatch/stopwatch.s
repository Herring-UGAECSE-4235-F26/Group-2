@This is a delay program.  Your RP4 either runs at 1.5 or 1.8GHZ.  Using the program determine what speed your PI appears to be running
@Deliverable 1:  Calculate your RP4 clock speed.  Show how.
@Now let's make a stopwatch.   Use RP9:RP8:RP7 for minutes:seconds:hundredths of seconds.  Output the free running time to the terminal.  At 2:00:00, the stopwatch should go back to 0:00:00 and keep running.  The display
@output should be as described.
@You should look at the printloop.s example for the use of the printf command.  You will probably need to investigate formating using the asciz data type.
@Deliverable 2: your code on github and in your writeup.  We will check in class and look at accuracy as well.
	.data

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

resetClock:
	mov r7, #0
	mov r8, #0
	mov r9, #0

resetNano:
	bl printCount
	ldr r6, =nanoCounter	@ load address of nanoCounter into r1
	ldr r6, [r6] @load value of address into r1

nanoLoop:
	subs r6, r6, #1		@ r1 = r1 – 1, decrement r1 (inner loop) 
	bne	nanoLoop			@ repeat it until r1 = 0 
	add	r7, r7, #1		@ r7 = r7 + 1, increment r7 (outer loop)
	ldr r4, =hundredthsSec	@ load address of hundredthsSec into r4
	str r7, [r4]	@ store value of r7 into value of hundredthsSec mem location
	cmp r7, #100	@subtracts 100 from r7 and sets the flags
	bne	resetNano			@ repeat it until r7 = 100

	add r8, r8, #1	@adds one to seconds
	ldr r4, =seconds	@ load address of seconds into r4
	str r8, [r4]	@ store value of r8 into value of seconds mem location
	cmp r8, #60
	bne resetNano

	add r9, r9, #1	@adds one to minutes
	ldr r4, =minutes	@ load address of minutes into r4
	str r9, [r4]	@ store value of r9 into value of minutes mem location
	cmp r9, #2	@subtracts 2 from r9 and sets the flags
	bne resetNano

	beq resetClock	@resets clock upon reaching 2 mins

	mov r7, #1
	svc 0
