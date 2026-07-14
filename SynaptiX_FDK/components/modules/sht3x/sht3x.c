#include "sht3x.h"
#include "sx_board.h"
#include <string.h>
#include "sx_delay.h"

static uint8_t calculate_crc(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* Send 2 bytes command */
static SHT3X_STATUS_T sht3x_send_cmd(SHT3X_T *sht3x, SHT3X_COMMAND_T cmd)
{
    uint8_t buf[2] = {(cmd >> 8) & 0xFF, cmd & 0xFF};
    return sx_i2c_write(sht3x->i2c, SHT3X_DEVICE_ADDR, buf, 2) == 0 ? SHT3X_OK : SHT3X_ERR;
}

SHT3X_STATUS_T sht3x_init(SHT3X_T *sht3x, sx_i2c_t *i2c)
{
    if (sht3x == NULL || i2c == NULL) return SHT3X_ERR;

    sht3x->i2c = i2c;
    sht3x->humidity = 0.0f;
    sht3x->temperature = 0.0f;

    // Soft reset
    if (sht3x_soft_reset(sht3x) != SHT3X_OK) {
        return SHT3X_ERR;
    }

    sx_delay_ms(20);  

    return SHT3X_OK;
}

SHT3X_STATUS_T sht3x_soft_reset(SHT3X_T *sht3x)
{
    return sht3x_send_cmd(sht3x, SHT3X_CMD_SOFT_RESET);
}

SHT3X_STATUS_T sht3x_read_status(SHT3X_T *sht3x, uint16_t *status)
{
    uint8_t buf[3];
    if (sht3x_send_cmd(sht3x, SHT3X_CMD_READ_STATUS) != SHT3X_OK) {
        return SHT3X_ERR;
    }

    sx_delay_ms(1);

    if (sx_i2c_read(sht3x->i2c, SHT3X_DEVICE_ADDR, buf, 3) != 0) {
        return SHT3X_ERR;
    }

    if (buf[2] != calculate_crc(buf, 2)) {
        return SHT3X_ERR_CRC;
    }

    *status = (buf[0] << 8) | buf[1];
    return SHT3X_OK;
}

SHT3X_STATUS_T sht3x_measure_single_shot(SHT3X_T *sht3x, SHT3X_COMMAND_T cmd)
{
    return sht3x_send_cmd(sht3x, cmd);
}

SHT3X_STATUS_T sht3x_read_measurement(SHT3X_T *sht3x)
{
    uint8_t buf[6];
    if (sx_i2c_read(sht3x->i2c, SHT3X_DEVICE_ADDR, buf, 6) != 0) {
        return SHT3X_ERR;
    }

    //Check CRC
    if (buf[2] != calculate_crc(buf, 2) || buf[5] != calculate_crc(buf + 3, 2)) {
        return SHT3X_ERR_CRC;
    }

    uint16_t raw_temp = (buf[0] << 8) | buf[1];
    uint16_t raw_hum  = (buf[3] << 8) | buf[4];

    sht3x->temperature = -45.0f + 175.0f * (raw_temp / 65535.0f);
    sht3x->humidity    = 100.0f * (raw_hum / 65535.0f);

    return SHT3X_OK;
}