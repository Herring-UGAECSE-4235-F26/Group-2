#include <stdio.h>

#include "I2CDriver.h"
#include <time.h>

#define RTC_ADDR 0x68

uint8_t dec_to_bcd(int val) {
	return ((val / 10) << 4) | (val % 10);
}

int bcd_to_dec(uint8_t val) {
	return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_set_time() {
	//Returns the Pi's current time
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	
	uint8_t data[7];
	data[0] = dec_to_bcd(t->tm_sec);
	data[1] = dec_to_bcd(t->tm_min);

	int hour;
	if(t->tm_hour == 0) {
		hour = 12;
	} else if (t->tm_hour > 12) {
		hour = t->tm_hour - 12;
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

    i2c_write_buffer(RTC_ADDR, data);   // Writes the 7-byte buffer to the RTC
}

// Reads the time from registers 00-06 and displays it on the terminal
void rtc_read_time() {
    uint8_t data[7];

    i2c_read_buffer(RTC_ADDR, data);    // Read the 7-byte buffer from the RTC
    
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