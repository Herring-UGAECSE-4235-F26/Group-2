# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lE4235

# Target executable name
TARGET = parmco_cp1

# Object files
OBJS = main.o motor_driver.o

# Default target
all: $(TARGET)

# Link the object files and the ECSE 4235 library
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Compile the C top-level controller
main.o: main.c
	$(CC) $(CFLAGS) -c main.c

# Compile the ARM assembly motor driver
motor_driver.o: motor_driver.s
	$(CC) $(CFLAGS) -c motor_driver.s

# Clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)