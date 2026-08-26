CC=gcc
CFLAGS=-lE4235
DEP=funcs.h

OBJ=main.o funcs.o
%.o: %.c $(DEP)
	$(CC) -c -o $@ $< $(CFLAGS)

main: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

run: main
	./main

clean:
	rm -f main
