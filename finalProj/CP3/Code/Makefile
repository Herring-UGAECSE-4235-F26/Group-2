# PARMCO CP3 controller (AI-generated)
# Requires on the Pi:  sudo apt install libbluetooth-dev libglib2.0-dev
CC      = gcc
CFLAGS  = -Wall -O2 $(shell pkg-config --cflags gio-2.0)
LDLIBS  = -lE4235 -pthread -lbluetooth $(shell pkg-config --libs gio-2.0) -lm

TARGET  = parmco_cp3
OBJS    = parmco_cp3_controller.o motor_driver.o as5600.o ble_server.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

motor_driver.o: motor_driver.s
	$(CC) -c motor_driver.s -o motor_driver.o

run: $(TARGET)
	sudo ./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
