# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lE4235 -pthread

# Target executable name
TARGET = parmco_cp2

# Object files
OBJS = main.o motor_driver.o

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

motor_driver.o: motor_driver.s
	$(CC) $(CFLAGS) -c motor_driver.s

clean:
	rm -f $(OBJS) $(TARGET)
