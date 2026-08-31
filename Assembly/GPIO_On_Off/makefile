OBJS = LinuxSquareWave.o
ifdef DEBUG
DEBUGFLGS = -g
else
DEBUGFLGS =
endif

%.o : %.s
	as $(DEBUGFLGS) $< -o $@
LinuxSquareWave: $(OBJS)
	gcc -o LinuxSquareWave $(OBJS)

clean:
	rm *.o LinuxSquareWave
