#include <stdio.h>
#include <stdint.h>

#include <E4235.h>

// GPIO pin definitions
int SDA_PIN = 2; // GPIO pin for SDA
int SCL_PIN = 3; // GPIO pin for SCL
int i2c_clk_delay = 100; // Delay for clock speed control in microseconds
int i2c_clk_half_delay = 50; // Half of the clock delay for timing

int SLAVE_ADDRESS = 0x50; // I2C slave address

char read_byte_buff[8] = {0}; // Buffer to hold data read from I2C device
char write_byte_buff[8] = {0}; // Buffer to hold data to write to I2C device


//////////////////////////////////////////////*GPIO pin control functions for active low/open drain operation.*/

// Set the specified GPIO pin "high". Done by floating the pin
void gpio_release(uint8_t pin) { E4235_GPIO_Select(pin, 0); } //pin output disconnected by setting as input

// Drive pin as "low" by writing a 0 to the pin
void gpio_drive_low(uint8_t pin) {
    E4235_GPIO_Select(pin, 1); // Set pin as output
    E4235_GPIO_WRITE(pin, 0); //write 0 to pin
}

//////////////////////////////////////////////*I2C SDA and SCL pin high/low drive functions. */

void sda_high() { gpio_release(SDA_PIN); } // Release SDA line
void sda_low() { gpio_drive_low(SDA_PIN); } // Drive SDA line low
void scl_high() { gpio_release(SCL_PIN); } // Release SCL line
void scl_low() { gpio_drive_low(SCL_PIN); } // Drive SCL line low

//////////////////////////////////////////////*I2C setup functions.*/

// Initiates the gpio pins for i2c by setting them to high. i2c pins are active low.
void i2c_init_pins() {
    sda_high();
    scl_high();
}

// Determines clock speed for i2c coms using user input.
void i2c_set_clk_speed(uint32_t speed) {
    
}

// Function to start i2c transmission. i2c start condition:SDA pulled low before SCL 
void i2c_start_message() {
    sda_high(); // Ensure SDA is high
    scl_high(); // Ensure SCL is high
    sda_low();
    E4235_Delay_us(i2c_clk_half_delay); // Delay for half clock period
    scl_low();
}

// Function to stop i2c transmission. i2c stop condition: SDA pulled high after SCL
void i2c_stop_message() {
    sda_low(); // Ensure SDA is low
    scl_high();
    E4235_Delay_us(i2c_clk_half_delay); // Delay for half clock period
    sda_high();
}

void i2c_write_byte(write_byte_buff) {
    for (int i = 0; i < 8; i++) {
        if ((write_byte_buff & (1 << (7 - i))) != 0) {
            sda_high();
        } else {
            sda_low();
        }
        scl_high(); // Clock high
        E4235_Delay_us(i2c_clk_delay); // Delay for half clock period
        scl_low();  // Clock low
    }
    // After sending the byte, release SDA for ACK/NACK
    sda_high();
    scl_high(); // Clock high for ACK/NACK
    scl_low();  // Clock low
}
