OBJS = stopwatch.o
DEPS = printloop.o

ifdef DEBUG
DEBUGFLGS = -g
else
DEBUGFLGS =
endif

%.o : %.s
	as $(DEBUGFLGS) $< -o $@
main: $(OBJS) $(DEPS)
	gcc -o printloop $(OBJS) $(DEPS)
run: main
	./printloop
clean:
	rm *.o printloop
