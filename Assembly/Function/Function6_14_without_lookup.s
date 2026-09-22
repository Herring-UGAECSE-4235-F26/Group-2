.text
.global _start

@Calculates mathematically x^2 + 2x + 3
_start:
	mov r9, #3			@Sets x = 3

	mul r10, r9, r9 	@r10 = x^2

	add r10, r10, r9, lsl #1	@r10 = x^2 + 2x
	add r10, r10, #3	@r10 = x^2 +2x + 3

	mov r7, #1
	svc 0
