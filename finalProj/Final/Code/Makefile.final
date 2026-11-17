# PARMCO final controller (AI-generated)
# Requires on the Pi:  sudo apt install libbluetooth-dev libglib2.0-dev
CC      = gcc
CFLAGS  = -Wall -O2 $(shell pkg-config --cflags gio-2.0)
LDLIBS  = -lE4235 -pthread -lbluetooth $(shell pkg-config --libs gio-2.0) -lm

TARGET  = parmco_final
OBJS    = parmco_final_controller.o motor_driver.o as5600.o ble_server.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

motor_driver.o: motor_driver.s
	$(CC) -c motor_driver.s -o motor_driver.o

# Run by hand (stops the boot service first so the two do not fight over GPIO/BLE)
run: $(TARGET)
	-sudo systemctl stop parmco 2>/dev/null
	sudo ./$(TARGET)

# Boot service helpers (see install.sh)
install: $(TARGET)
	sudo ./install.sh

status:
	systemctl status parmco --no-pager

logs:
	journalctl -u parmco -f

restart: $(TARGET)
	sudo systemctl restart parmco

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run install status logs restart clean
