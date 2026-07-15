#include "app.h"
#include "logger.h"
#include "sx_delay.h"
#include "sx_board.h"
#include "sps30_app.h"
#include "sx_temp_humi.h"
#include "sx_sleep_manager.h"
#include "gas_sensor_app.h"

static const char *TAG = "APP";

/* App-layer instances live here, not in Board_t (sx_board.h) — Board_t
 * only owns concrete driver/hardware handles (sht3x, sps30_uart, gps...);
 * these are the "how this app uses that hardware" layer, per discussion
 * with the user on where sps30_app_t/sx_temp_humi_t/sx_sleep_manager_t
 * (tier 3 of the sleep architecture) should live. */
static sps30_app_t       s_sps30_app;
static sx_temp_humi_t    s_temp_humi;
static sx_sleep_manager_t s_sleep_mgr;
/* No gas_sensor_app_t instance — gas_sensor_app.c has no state of its
 * own, see gas_sensor_app.h's doc-comment (all runtime state already
 * lives in ze12a.c's statics + the gas_sensor[] array it owns). */

void app_init(void){
    log_info(TAG, "APP initializing ....");

    sps30_app_init(&s_sps30_app, sx_board_get_sps30_power_gpio());
    sx_temp_humi_init(&s_temp_humi, &board.sht3x);

    sx_sleep_manager_init(&s_sleep_mgr, &board.sleep, &board.modem, &board.gps,
                           &s_sps30_app, sx_board_get_pump_gpio());
}
void app_process(uint32_t delta_ms){
    /* Only the sensors that need per-tick driving from the app layer.
     * gas_sensor_app_poll() must run every tick regardless of the main
     * app state machine (not yet written — see next_prompt.md item 6)
     * so ZE12A's mux round-robin + byte assembly keeps advancing and
     * its GAS_SENSOR_TIMEOUT_MS connection-loss detection stays
     * accurate, the same reasoning as gps_process()/sim polling
     * elsewhere in this project running unconditionally every tick. */
    gas_sensor_app_poll(delta_ms);
}