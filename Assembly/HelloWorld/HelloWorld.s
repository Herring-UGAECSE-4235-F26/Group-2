@Assembler program to print "Hello World" to stdout.
@R0-R2- parameters to linux function services 
@R7 - linux function number

@Provide program starting point
.global _start                       
@address to linker 'Not sure about this'

@Set up the parameters to print hello world 
@and then call linux to do it
#changed length of the out string to #26 instead of #13 to make the ascii message fit
_start:     mov     R0, #1          @ 1 = StdOut
            ldr     R1, =helloworld @sting to print
            mov     R2, #26         @length of out string
            mov     R7, #4          @linux write system call
            svc     0               @call linux to print

@set up the parameters to exit the program
@and then call linux to do it
            mov     R0, #0          @use 0 return code
            mov     R7, #1          @service command code 1
                                    @terminates this program
            svc     0               @call linux to terminate

.data

#Maya made this one
helloworld: .ascii "Hello world from Group 2\n"
