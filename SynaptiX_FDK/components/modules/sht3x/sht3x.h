#ifndef SHT3X_H
#define SHT3X_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sx_i2c.h"

#define SHT3X_DEVICE_ADDR       (0x44<<1)    // ADDR pin connect to GND
#define SHT3X_TIMEOUT_MS        50

typedef struct {
    sx_i2c_t *i2c;
    float humidity;
    float temperature;
} SHT3X_T;

typedef enum{
    SHT3X_OK = 0,
    SHT3X_ERR,
    SHT3X_ERR_CRC,
    SHT3X_ERR_TIMEOUT
} SHT3X_STATUS_T;

/* Commands */
typedef enum {
    SHT3X_CMD_MEASURE_HIGHREP_STRETCH = 0x2C06,
    SHT3X_CMD_MEASURE_MEDREP_STRETCH  = 0x2C0D,
    SHT3X_CMD_MEASURE_LOWREP_STRETCH  = 0x2C10,

    SHT3X_CMD_MEASURE_HIGHREP_10HZ    = 0x2737,
    SHT3X_CMD_MEASURE_LOWREP_10HZ     = 0x272A,

    SHT3X_CMD_FETCH_DATA              = 0xF000,
    SHT3X_CMD_READ_STATUS             = 0xF32D,
    SHT3X_CMD_CLEAR_STATUS            = 0x3041,
    SHT3X_CMD_SOFT_RESET              = 0x30A2,
    SHT3X_CMD_HEATER_ENABLE           = 0x306D,
    SHT3X_CMD_HEATER_DISABLE          = 0x3066,
    SHT3X_CMD_BREAK                   = 0x3093
} SHT3X_COMMAND_T;

SHT3X_STATUS_T sht3x_init(SHT3X_T *sht3x, sx_i2c_t *i2c);
SHT3X_STATUS_T sht3x_soft_reset(SHT3X_T *sht3x);
SHT3X_STATUS_T sht3x_read_status(SHT3X_T *sht3x, uint16_t *status);
SHT3X_STATUS_T sht3x_measure_single_shot(SHT3X_T *sht3x, SHT3X_COMMAND_T cmd);
SHT3X_STATUS_T sht3x_read_measurement(SHT3X_T *sht3x);

#ifdef __cplusplus
}
#endif
#endif