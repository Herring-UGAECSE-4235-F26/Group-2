#include "bcm2835.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define RTC_ADDR 0x68

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

	//returns day of the week
	data[3] = t->tm_wday + 1;
	//returns day of the month
	data[4] = dec_to_bcd(t -> tm_mday)
	//day
