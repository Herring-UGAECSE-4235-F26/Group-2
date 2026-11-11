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

# Bluetooth server configuration
BT_SOURCE = bluetooth_server.c
BT_TARGET = bluetooth_server

.PHONY: bluetooth-build bluetooth-setup bluetooth-run bluetooth-stop

# Compile the Bluetooth server
bluetooth-build:
	gcc -Wall -Wextra -O2 $(BT_SOURCE) -o $(BT_TARGET) -lbluetooth

# Enable Bluetooth and register the Serial Port service
bluetooth-setup:
	sudo systemctl start bluetooth
	sudo rfkill unblock bluetooth
	sudo bluetoothctl power on
	sudo sdptool add --channel=1 SP

# Compile, configure, and start the Bluetooth server
bluetooth-run: bluetooth-build bluetooth-setup
	sudo ./$(BT_TARGET)

# Stop the Bluetooth server
bluetooth-stop:
	sudo pkill -f "^./$(BT_TARGET)$$" || true
