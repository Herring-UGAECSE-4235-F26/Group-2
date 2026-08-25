#HelloWorld: HelloWorld.o
#	ld -o HelloWorld HelloWorld.o
#HelloWorld.o: HelloWorld.s
#	as -o HelloWorld.o HelloWorld.s


CC=gcc
DEBUG=-g

TARGET=HelloWorld.s

#pattern rule. creates <target.o> from <source.s>
#%.o: %.s
#gcc -c <source.s> -o <target.o>
#	$(CC) -c $< -o $@

#compiles and creates exe from first argument in target var with debug flag.
#saves exe as "main"
main: $(TARGET)
#gcc -g main.s -o main
	$(CC) $(DEBUG) $< -o $@

#run rule
run: main
	./main

#cleans dir. removes <source.s> and exe <main> files
clean:
	rm -f $(TARGET) main
