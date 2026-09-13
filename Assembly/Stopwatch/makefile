OBJS = stopwatchCP2.o E4235_KYBdeblock.o

ifdef DEBUG
DEBUGFLGS = -g
else
DEBUGFLGS =
endif

%.o : %.s
	as $(DEBUGFLGS) $< -o $@
main: $(OBJS)
	gcc -o stopwatchCP2 $(OBJS) $(DEPS)
run: main
	./stopwatchCP2
clean:
	rm *.o stopwatchCP2
