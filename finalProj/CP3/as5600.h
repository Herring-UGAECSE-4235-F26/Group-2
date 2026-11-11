#ifndef AS5600_H
#define AS5600_H

#include <stdint.h>

int as5600_init(void);
void as5600_close(void);
int as5600_read_raw(uint16_t *raw_angle);
int as5600_read_degrees(float *degrees);

#endif
