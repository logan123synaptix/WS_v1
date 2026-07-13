#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define GAS_SENSOR_COUNT 5

typedef enum GasSensorType{
    GAS_SENSOR_CO = 0x04,
    GAS_SENSOR_SO2 = 0x2B,
    GAS_SENSOR_NO2 = 0x2C,
    GAS_SENSOR_O3 = 0x2A,
    GAS_SENSOR_H2S =  0x03
}GasSensorType_t;

typedef struct GasSensor{
    GasSensorType_t type;
    struct GasFrame
    {
        /* data */
        uint8_t start;
        uint8_t addr;
        uint8_t command;
        uint8_t payload[4];
        uint8_t crc;
    }Frame;
    uint16_t value;
    bool isConnected;
    uint8_t unitPPB;
    uint32_t timeout;
}GasSensor_t;

void gas_sensor_init();
void gas_sensor_poll(uint32_t time_stamp);
int gas_sensor_read_value(GasSensor_t *sensor, uint16_t *value);
void gas_sensor_start_calibration(GasSensor_t *sensor, bool isZeroCalib);

extern GasSensor_t gas_sensor[GAS_SENSOR_COUNT];

#ifdef __cplusplus
}
#endif
#endif