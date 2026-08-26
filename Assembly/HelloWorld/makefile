#HelloWorld: HelloWorld.o
#	ld -o HelloWorld HelloWorld.o
#HelloWorld.o: HelloWorld.s
#	as -o HelloWorld.o HelloWorld.s


CC=gcc
DEBUG=-g
#CFLAGS=-lE4235
#DEP=funcs.h

TARGET=HelloWorld.s

#pattern rule. creates <target.o> from <source.s>
#%.o: %.s
#gcc -c <source.s> -o <target.o>
#	$(CC) $(DEBUG) $< -o $@

#compiles and creates exe from first argument in target var with debug flag.
#saves exe as "main"
main: $(TARGET)
#gcc -g main.s -o main
	$(CC) $(TARGET) -o $@

#run rule
run: main
	./main

#cleans dir. removes exe <main> files
clean:
	rm -f HelloWorld