#include "gas_sensor_app.h"

/* Finds the gas_sensor[] slot matching type, or NULL if type isn't one
 * of the GAS_SENSOR_COUNT known types gas_sensor_init() populated (see
 * ze12a.c: gas_sensor[] is fixed-size and pre-filled with exactly
 * {CO, SO2, NO2, O3, H2S} at init — this never changes at runtime, so a
 * linear scan here is cheap and there is nothing to cache). */
static GasSensor_t *gas_sensor_app_find(GasSensorType_t type)
{
    for (uint8_t i = 0; i < GAS_SENSOR_COUNT; i++) {
        if (gas_sensor[i].type == type) {
            return &gas_sensor[i];
        }
    }
    return NULL;
}

void gas_sensor_app_poll(uint32_t delta_ms)
{
    gas_sensor_poll(delta_ms);
}

bool gas_sensor_app_is_connected(GasSensorType_t type)
{
    GasSensor_t *s = gas_sensor_app_find(type);
    return (s != NULL) && s->isConnected;
}

uint16_t gas_sensor_app_get_value(GasSensorType_t type)
{
    GasSensor_t *s = gas_sensor_app_find(type);
    if (s == NULL || !s->isConnected) {
        return 0;
    }
    return s->value;
}

uint8_t gas_sensor_app_get_unit(GasSensorType_t type)
{
    GasSensor_t *s = gas_sensor_app_find(type);
    if (s == NULL) {
        return 0;
    }
    return s->unitPPB;
}