#include <stdio.h>

#include "I2CDriver.h"
#include <time.h>

uint8_t dec_to_bcd(int val) {
	return ((val / 10) << 4) | (val % 10);
}

int bcd_to_dec(uint8_t val) {
	return ((val >> 4) * 10) + (val & 0x0F);
}

void setRTC() {
	//Returns the Pi's current time
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	
	uint8_t data[7];
	data[0] = dec_to_bcd(t->tm_sec);
	data[1] = dec_to_bcd(t->tm_min);

	int hour;
	if(t->tm_hour == 0) {
		hour = 12;
	} else if (t->tm_hour >12) {
		hour = tm_hour - 12;
	} else {
		hour = t->tm_hour;
	}

	data[2] = dec_to_bcd(hour);
	data[2] |= 0x40;

	if(t->tm_hour >= 12) {
		data[2] |= 0x20;
	}

	// Format Day, Date, Month, Year (Registers 03 to 06)
    data[3] = t->tm_wday + 1;             // 1-7 for Sunday-Saturday
    data[4] = dec_to_bcd(t->tm_mday);     // 1-31
    data[5] = dec_to_bcd(t->tm_mon + 1);  // 1-12 (tm_mon is 0-11)
    data[6] = dec_to_bcd(t->tm_year % 100); // 2-digit year
    
    // Begin the I2C transmission
    i2c_start_message();
    
    // Send the Slave Address with the Write bit (0)
    // 0x68 shifted left by 1 is 0xD0
    i2c_write_byte(SLAVE_ADDRESS << 1);
    
    // Send the starting register address (0x00)
    i2c_write_byte(0x00);
    
    // Burst write the 7 bytes of time data
    for (int i = 0; i < 7; i++) {
        i2c_write_byte(data[i]);
    }
    
    // Release the bus
    i2c_stop_message();
}

// Reads the time from registers 00-06 and displays it on the terminal
void rtc_read_time() {
    uint8_t data[7];
    
    // Set the internal register pointer to 0x00
    i2c_start_message();
    i2c_write_byte(0x68 << 1); // Slave address + Write bit (0xD0)
    i2c_write_byte(0x00);      // Point to Register 00 (Seconds)

    // Issue a Repeated Start to switch to Read mode
    i2c_start_message();
    i2c_write_byte((0x68 << 1) | 1); // Slave address + Read bit (0xD1)
    
    // Read registers 00 through 06
    for (int i = 0; i < 7; i++) {
        // Send ACK (1) for first 6 bytes, NACK (0) for the 7th byte
        int send_ack = (i < 6) ? 1 : 0;
        data[i] = i2c_read_byte(send_ack);
    }

    i2c_stop_message();
    
    // Parse the BCD data back to Decimal
    // Masking out the control bits to isolate the BCD values
    int sec = bcd_to_dec(data[0] & 0x7F);
    int min = bcd_to_dec(data[1] & 0x7F);

    // Extract 12-hour format hour and AM/PM flag
    int is_pm = (data[2] & 0x20) ? 1 : 0; // Check bit 5 for PM flag
    int hour = bcd_to_dec(data[2] & 0x1F); // Mask out bits 5 and 6
    
    int date = bcd_to_dec(data[4] & 0x3F);
    int month = bcd_to_dec(data[5] & 0x1F); // Bit 7 is the century flag
    int year = bcd_to_dec(data[6]) + 2000;
    
    // Display to terminal
    printf("Current RTC Time: %02d/%02d/%04d %02d:%02d:%02d %s\n", 
           month, date, year, hour, min, sec, is_pm ? "PM" : "AM");
}

int main() {
    int choice;
    
    // Initialize the GPIO pins for the driver before doing anything
    i2c_init_pins();
    
    while (1) {
        printf("\n--- RTC I2C Driver Menu ---\n");
        printf("1. Read time from RTC\n");
        printf("2. Write system time to RTC\n");
        printf("3. Exit\n");
        printf("Enter choice: ");
        
        // Read user input
        if (scanf("%d", &choice) != 1) {
            // Clear the input buffer if the user enters a non-integer
            while (getchar() != '\n'); 
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        
        // Route to the appropriate function based on the user's choice
        if (choice == 1) {
            rtc_read_time();
        } else if (choice == 2) {
            rtc_set_time();
            printf("Success: Current system time written to the RTC.\n");
        } else if (choice == 3) {
            printf("Exiting driver...\n");
            break;
        } else {
            printf("Invalid choice. Please enter 1, 2, or 3.\n");
        }
    }
    
    return 0;
}