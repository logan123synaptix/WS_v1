#include "ads1115.h"
#include "sx_delay.h"

/* Voltage-per-LSB coefficient (mV) for each PGA setting, straight from the
 * TI datasheet's Full-Scale-Range / 32768 table. Same values and mapping
 * as the reference WS_v0 driver. */
static float ads1115_volt_coef_for_pga(uint16_t pga)
{
    switch (pga) {
    case ADS1115_PGA_TWOTHIRDS: return 0.1875f;
    case ADS1115_PGA_ONE:       return 0.125f;
    case ADS1115_PGA_TWO:       return 0.0625f;
    case ADS1115_PGA_FOUR:      return 0.03125f;
    case ADS1115_PGA_EIGHT:     return 0.015625f;
    case ADS1115_PGA_SIXTEEN:   return 0.0078125f;
    default:                    return 0.0625f; /* falls back to PGA_TWO's coefficient */
    }
}

ADS1115_STATUS_T ADS1115_Init(ADS1115_T *ads1115, sx_i2c_t *i2c, uint16_t setDataRate, uint16_t setPGA)
{
    if (ads1115 == NULL || i2c == NULL) return ADS1115_ERR;

    ads1115->i2c = i2c;
    ads1115->dataRate = setDataRate;
    ads1115->pga = setPGA;
    ads1115->voltCoef = ads1115_volt_coef_for_pga(setPGA);

    return sx_i2c_is_device_ready(i2c, ADS1115_DEVICE_ADDR, 5, 10) == 0
               ? ADS1115_OK
               : ADS1115_ERR;
}

ADS1115_STATUS_T ADS1115_readSingleEnded(ADS1115_T *ads1115, uint16_t muxPort, float *voltage)
{
    if (ads1115 == NULL || voltage == NULL) return ADS1115_ERR;

    uint8_t config[2];
    config[0] = ADS1115_OS | muxPort | ads1115->pga | ADS1115_MODE;
    config[1] = ads1115->dataRate | ADS1115_COMP_MODE | ADS1115_COMP_POL | ADS1115_COMP_LAT | ADS1115_COMP_QUE;

    /* Kick off a single-shot conversion on the requested mux channel. */
    if (sx_i2c_mem_write(ads1115->i2c, ADS1115_DEVICE_ADDR, ADS1115_CONFIG_REG, 1, config, 2) != 0) {
        return ADS1115_ERR;
    }

    /* Poll the OS bit (config register, MSB of byte 0) until the ADC
     * reports the conversion is done. Per datasheet: OS reads 0 while a
     * conversion is in progress and 1 once it completes. */
    uint16_t attempts = 0;
    uint8_t status[2];
    for (;;) {
        if (sx_i2c_mem_read(ads1115->i2c, ADS1115_DEVICE_ADDR, ADS1115_CONFIG_REG, 1, status, 2) != 0) {
            return ADS1115_ERR;
        }
        if (status[0] & ADS1115_OS) {
            break;
        }
        if (++attempts >= ADS1115_MAX_POLL_ATTEMPTS) {
            return ADS1115_ERR_TIMEOUT;
        }
        sx_delay_ms(1);
    }

    uint8_t raw[2];
    if (sx_i2c_mem_read(ads1115->i2c, ADS1115_DEVICE_ADDR, ADS1115_CONVER_REG, 1, raw, 2) != 0) {
        return ADS1115_ERR;
    }

    int16_t raw_value = (int16_t)((raw[0] << 8) | raw[1]);
    *voltage = (float)raw_value * ads1115->voltCoef;

    return ADS1115_OK;
}