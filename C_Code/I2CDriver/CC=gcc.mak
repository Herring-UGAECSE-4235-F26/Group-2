CC=gcc
DEP=I2CDriver.h
FLAG=-lE4235

OBJ=setRTC.c

main: $(OBJ)
	$(CC) $(DEP) $(OBJ) -o main $(FLAG)

run: main
	./main

clean:
	rm -f main