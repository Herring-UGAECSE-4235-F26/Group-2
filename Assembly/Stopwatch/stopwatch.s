@This is a delay program.  Your RP4 either runs at 1.5 or 1.8GHZ.  Using the program determine what speed your PI appears to be running
@Deliverable 1:  Calculate your RP4 clock speed.  Show how.
@Now let's make a stopwatch.   Use RP9:RP8:RP7 for minutes:seconds:hundredths of seconds.  Output the free running time to the terminal.  At 2:00:00, the stopwatch should go back to 0:00:00 and keep running.  The display
@output should be as described.
@You should look at the printloop.s example for the use of the printf command.  You will probably need to investigate formating using the asciz data type.
@Deliverable 2: your code on github and in your writeup.  We will check in class and look at accuracy as well.

	.text
	
	extern main

	.global _start


_start:	

	mov	r2, #15		@ load 15 into r2 (outer loop count)

l1:	ldr	r1, =1000000000	@ r1 = 1,000,000,000 (inner loop count) 
l2:	bl main
	subs	r1, r1, #1		@ r1 = r1 – 1, decrement r1 (inner loop) 
	bne	l2			@ repeat it until r1 = 0 
	subs	r2, r2, #1		@ r2 = r2 – 1, decrement r2 (outer loop) 
	bne	l1			@ repeat it until r2 = 0 

	@ terminate the program
	mov   	r7, #1
	svc   0