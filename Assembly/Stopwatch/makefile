OBJS = stopwatchV2.o

ifdef DEBUG
DEBUGFLGS = -g
else
DEBUGFLGS =
endif

%.o : %.s
	as $(DEBUGFLGS) $< -o $@
main: $(OBJS)
	gcc -o printloop $(OBJS) $(DEPS)
run: main
	./stopwatchV2
clean:
	rm *.o stopwatchV2
