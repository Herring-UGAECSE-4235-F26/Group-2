#include <stdio.h>
#include <stdint.h>

#include "I2CDriver.h"
#include <E4235.h>

// GPIO pin definitions
int SDA_PIN = 17; // GPIO pin for SDA
int SCL_PIN = 27; // GPIO pin for SCL
int i2c_clk_delay = 1000; // Delay for clock speed control in microseconds
int i2c_clk_half_delay = 500; // Half of the clock delay for timing

int SLAVE_ADDRESS = 0x68; // I2C slave address

char read_byte_buff[8] = {0}; // Buffer to hold data read from I2C device
char write_byte_buff[8] = {0}; // Buffer to hold data to write to I2C device


//////////////////////////////////////////////*GPIO pin control functions for active low/open drain operation.*/

// Set the specified GPIO pin "high". Done by floating the pin
void gpio_release(uint8_t pin) { E4235_Select(pin, 0); } //pin output disconnected by setting as input

// Drive pin as "low" by writing a 0 to the pin
void gpio_drive_low(uint8_t pin) {
    E4235_Select(pin, 1); // Set pin as output
    E4235_Write(pin, 0); //write 0 to pin
}

//////////////////////////////////////////////*I2C SDA and SCL pin high/low drive functions. */

void sda_high() { gpio_release(SDA_PIN); } // Release SDA line
void sda_low() { gpio_drive_low(SDA_PIN); } // Drive SDA line low
void scl_high() { gpio_release(SCL_PIN); } // Release SCL line
void scl_low() { gpio_drive_low(SCL_PIN); } // Drive SCL line low

//////////////////////////////////////////////*I2C setup functions.*/

// Initiates the gpio pins for i2c by setting them to high. i2c pins are active low.
void i2c_init_pins() {
    E4235_Init();
    sda_high();
    scl_high();
}

// Function to start i2c transmission. i2c start condition:SDA pulled low before SCL 
void i2c_start_message() {
    sda_high(); // Ensure SDA is high
    scl_high(); // Ensure SCL is high
    sda_low();
    E4235_Delaymicro(i2c_clk_half_delay); // Delay for half clock period
    scl_low();
}

// Function to stop i2c transmission. i2c stop condition: SDA pulled high after SCL
void i2c_stop_message() {
    sda_low(); // Ensure SDA is low
    scl_high();
    E4235_Delaymicro(i2c_clk_half_delay); // Delay for half clock period
    sda_high();
}

// Function to write a byte to the I2C bus
int i2c_write_byte(uint8_t data) {
    int ack_bit;
    for (int i = 0; i < 8; i++) {   // Loop through each bit
        if ((data & (1 << (7 - i))) != 0) {
            sda_high();
        } else {
            sda_low();
        }
        scl_high(); // Clock high
        E4235_Delaymicro(i2c_clk_delay); // Delay for half clock period
        scl_low();  // Clock low
    }
    // After sending the byte, release SDA for ACK/NACK
    sda_high();
    scl_high(); // Clock high for ACK/NACK
    E4235_Delaymicro(i2c_clk_half_delay);
    ack_bit = E4235_Read(SDA_PIN);  // Read ACK/NACK bit
    scl_low();  // Clock low

    return ack_bit;
}

uint8_t i2c_read_byte(int ack) {
    uint8_t data = 0;
    sda_high(); // Release SDA for reading
    for (int i = 0; i < 8; i++) {   // Loop through each bit
        E4235_Delaymicro(i2c_clk_half_delay);
        scl_high(); // Clock high
        E4235_Delaymicro(i2c_clk_half_delay); // Delay for half clock period
        data <<= 1; // Shift left to make room for next bit
        if (E4235_Read(SDA_PIN)) {
            data |= 1; // Set LSB if SDA is high
        }
        scl_low();  // Clock low
    }
    
    if(ack){
        sda_low(); // Drive SDA low for ACK
    } else {
        sda_high(); // Release SDA for NACK
    }
    E4235_Delaymicro(i2c_clk_half_delay);
    scl_high();  // Clock high
    E4235_Delaymicro(i2c_clk_half_delay);
    scl_low();  // Clock low
    sda_high(); // Release SDA
    return data;
}

// Writes a 7-byte buffer to register 0x00 on the RTC
void i2c_write_buffer(uint8_t dev_addr, uint8_t *data) {
    i2c_start_message();
    i2c_write_byte(dev_addr << 1);  // Send Device Address + Write bit (0)
    i2c_write_byte(0x00);   // Send the starting register address
    for (int i = 0; i < 7; i++) {   // Write exactly 7 bytes
        i2c_write_byte(data[i]);
    }
    i2c_stop_message();
}

// Reads 7 bytes from register 0x00 on the RTC into a buffer
void i2c_read_buffer(uint8_t dev_addr, uint8_t *buffer) {
    // Write internal register pointer to 0x00
    i2c_start_message();
    i2c_write_byte(dev_addr << 1); // Device Address + Write bit (0)
    i2c_write_byte(0x00);
    
    // Repeated start to switch bus direction to read
    i2c_start_message();
    i2c_write_byte((dev_addr << 1) | 1); // Device Address + Read bit (1)
    for (int i = 0; i < 7; i++) {
        // Send ACK (1) for first 6 bytes, NACK (0) for the 7th byte
        int send_ack = (i < 6) ? 1 : 0;
        buffer[i] = i2c_read_byte(send_ack);
    }
    
    i2c_stop_message();
}
