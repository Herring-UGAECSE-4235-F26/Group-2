# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lE4235 -pthread -lbluetooth

# Target executable name
TARGET = parmco_cp3

# Object files
OBJS = parmco_cp3_controller.o motor_driver.o as5600.o

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

parmco_cp3_controller.o: parmco_cp3_controller.c
	$(CC) $(CFLAGS) -c parmco_cp3_controller.c

motor_driver.o: motor_driver.s
	$(CC) $(CFLAGS) -c motor_driver.s

clean:
	rm -f $(OBJS) $(TARGET)
