
#include "as5600.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define AS5600_ADDRESS 0x36
#define AS5600_RAW_ANGLE_HIGH 0x0C

static int encoder_fd = -1;

/* Open the Raspberry Pi I2C bus and select the AS5600. */
int as5600_init(void)
{
    encoder_fd = open("/dev/i2c-1", O_RDWR);

    if (encoder_fd < 0)
        return -1;

    if (ioctl(encoder_fd, I2C_SLAVE, AS5600_ADDRESS) < 0) {
        as5600_close();
        return -1;
    }

    return 0;
}

/* Close the encoder connection. */
void as5600_close(void)
{
    if (encoder_fd >= 0) {
        close(encoder_fd);
        encoder_fd = -1;
    }
}

/* Read the 12-bit raw encoder position (0-4095). */
int as5600_read_raw(uint16_t *raw_angle)
{
    unsigned char reg = AS5600_RAW_ANGLE_HIGH;
    unsigned char data[2];

    if (encoder_fd < 0 || raw_angle == NULL)
        return -1;

    if (write(encoder_fd, &reg, 1) != 1)
        return -1;

    if (read(encoder_fd, data, 2) != 2)
        return -1;

    *raw_angle = ((uint16_t)data[0] << 8 | data[1]) & 0x0FFF;

    return 0;
}

/* Convert raw encoder position into degrees (0 to <360). */
int as5600_read_degrees(float *degrees)
{
    uint16_t raw;

    if (degrees == NULL || as5600_read_raw(&raw) != 0)
        return -1;

    *degrees = raw * (360.0f / 4096.0f);

    return 0;
}
