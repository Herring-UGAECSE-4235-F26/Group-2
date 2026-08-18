CC=gcc
#CFLAGS=-I.
#DEP=funcs.h

OBJ=hello_world.o #funcs.o
%.o: %.c $(DEP)
	$(CC) -c -o $@ $< $(CFLAGS)

main: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

run: main
	./main

clean:
	rm -f $(OBJ) main
