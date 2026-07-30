#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    APP_MODE_FULL_POWER = 0,
    APP_MODE_ENTER_SLEEP,
    APP_MODE_SLEEP,
    APP_MODE_WAKEUP,
} app_mode_t;

//extern volatile app_mode_t app_mode;

void app_init(uint8_t is_wake_from_standby);

void app_process(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif