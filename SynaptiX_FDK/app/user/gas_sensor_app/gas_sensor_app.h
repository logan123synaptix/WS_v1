#ifndef GAS_SENSOR_APP
#define GAS_SENSOR_APP

#ifdef __cplusplus
extern "C" {
#endif

/*GAS SENSOR: ZE12A*/
#include "stdint.h"
#include "stdio.h"
#include "string.h"

typedef enum {
    GAS_SENSOR_STATE_IDLE = 0,
} gas_sensor_app_state_t;

#ifdef __cplusplus
}
#endif

#endif