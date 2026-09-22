#ifndef I2CDRIVER_H
#define I2CDRIVER_H

#include <stdint.h>

// Initializes the designated SDA and SCL pins
void i2c_init_pins();

// Sends the I2C Start condition
void i2c_start_message();

// Sends the I2C Stop condition
void i2c_stop_message();

// Writes a single byte to the bus and returns the ACK/NACK response bit
int i2c_write_byte(uint8_t data);

// Reads a single byte from the bus. Pass 1 to send an ACK, 0 to send a NACK.
uint8_t i2c_read_byte(int ack);

void i2c_write_buffer(uint8_t dev_addr, uint8_t *data);
void i2c_read_buffer(uint8_t dev_addr, uint8_t *buffer);

#endif