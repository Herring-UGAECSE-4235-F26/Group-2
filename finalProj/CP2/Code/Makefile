# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
# Added -pthread to link the POSIX threads library
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

# Target to automatically configure the IR sensor GPIO pins
setup_ir:
	@echo "Configuring GPIO 13 for IR sensor hardware interrupts..."
	-@sh -c 'echo 13 > /sys/class/gpio/export' 2>/dev/null || true
	@sleep 0.5
	@sh -c 'echo falling > /sys/class/gpio/gpio13/edge'
	@echo "GPIO 13 configured successfully."

clean:
	rm -f $(OBJS) $(TARGET)
