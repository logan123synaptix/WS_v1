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
    /* Mini-wake used to publish a heartbeat partway through a long
     * sleep_ms lap, without running the full wake sequence (no GPS fix
     * wait, no SPS30 measurement, no pump). See app.c's
     * APP_MODE_ENTER_SLEEP branch and sx_sleep_manager.c's
     * sx_sleep_manager_hb_only_*() functions for the state machine this
     * drives. */
    APP_MODE_HB_ONLY,
} app_mode_t;

//extern volatile app_mode_t app_mode;

void app_init(void);

void app_process(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif
