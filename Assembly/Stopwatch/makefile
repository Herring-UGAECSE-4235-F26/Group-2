OBJS = stopwatchCP1.o

ifdef DEBUG
DEBUGFLGS = -g
else
DEBUGFLGS =
endif

%.o : %.s
	as $(DEBUGFLGS) $< -o $@
main: $(OBJS)
	gcc -o stopwatchCP1 $(OBJS) $(DEPS)
run: main
	./stopwatchCP1
clean:
	rm *.o stopwatchCP1
