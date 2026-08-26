# Ask the system for its CPU architecture
ARCH := $(shell uname -m)

# If the architecture is the 32-bit Raspberry Pi
ifeq ($(ARCH), armv7l)
	CC = gcc
	AS = as
	EMU = 
	RUN_CMD = ./main
else
# Otherwise, we are inside the Docker container
	CC = arm-linux-gnueabihf-gcc
	AS = arm-linux-gnueabihf-as
endif

all: main

main: main.o
	$(CC) -g -o main main.o

main.o: main.s
	$(AS) -g -o main.o main.s

run: main
	$(RUN_CMD)

clean:
	rm -f main main.o