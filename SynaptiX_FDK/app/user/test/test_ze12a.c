#include "test_ze12a.h"
#include "gas_sensor_app.h"
#include "ze12a.h"
#include "logger.h"

static const char *TAG = "TEST_ZE12A";

static uint32_t s_log_accum_ms = 0;
#define ZE12A_LOG_PERIOD_MS  2500U

/* Per the user (2026-08-03): SO2/NO2 stopped showing any frames while O3
 * kept working, right after some prior run had gone through the
 * sleep sequence (sx_sleep_manager.c's _gas_sensor_qa_mode_start()
 * broadcasts CMD_SWITCH_TO_QA_MODE to all mux channels). Q&A mode is
 * stored non-volatile inside each module (see gas_sensor_switch_to_
 * active_mode()'s doc-comment in ze12a.h) — a module left in Q&A mode
 * stays silent forever against this test's read-only listening, which
 * matches "O3 still works, SO2/NO2 don't" if the broadcast reached some
 * channels but not others. Testing that theory: explicitly force all
 * modules back to Active Upload mode once at init, rather than assuming
 * they're already there. If this fixes SO2/NO2, the theory is confirmed
 * and this call should stay; if not, revert this and look elsewhere
 * (wiring/power to those two modules specifically). */
void test_ze12a_init(void)
{
    log_info(TAG, "=== TEST ZE12A (UART5 shared, mux round-robin, %u sensors) ===",
              GAS_SENSOR_COUNT);
    log_info(TAG, "Dwell %u ms/channel, timeout %u ms — allow at least %lu ms "
              "before expecting all channels connected",
              (unsigned)GAS_SENSOR_CHANNEL_DWELL_MS, (unsigned)GAS_SENSOR_TIMEOUT_MS,
              (unsigned long)(GAS_SENSOR_MUX_CHANNEL_COUNT * GAS_SENSOR_CHANNEL_DWELL_MS));

    gas_sensor_switch_to_active_mode();
    log_info(TAG, "Forced all mux channels to Active Upload mode (testing Q&A-mode-stuck theory)");
}

/* Static list so the log loop below can report a stable name per type
 * without duplicating the enum's raw hex values inline (matches the
 * GasSensorType_t enum in ze12a.h — SO2/NO2/O3 only, the three types
 * this board actually has modules for). */
typedef struct {
    GasSensorType_t type;
    const char     *name;
} gas_sensor_name_t;

static const gas_sensor_name_t s_names[GAS_SENSOR_COUNT] = {
    { GAS_SENSOR_SO2, "SO2" },
    { GAS_SENSOR_NO2, "NO2" },
    { GAS_SENSOR_O3,  "O3"  },
};

void test_ze12a_poll(uint32_t delta_ms)
{
    /* Drives the mux round-robin + byte-assembly state machine, same
     * call app.c's real gas_sensor_app_poll() makes every tick. */
    gas_sensor_app_poll(delta_ms);

    s_log_accum_ms += delta_ms;
    if (s_log_accum_ms < ZE12A_LOG_PERIOD_MS) {
        return;
    }
    s_log_accum_ms = 0;

    for (int i = 0; i < GAS_SENSOR_COUNT; i++) {
        GasSensorType_t type = s_names[i].type;
        bool connected = gas_sensor_app_is_connected(type);
        if (connected) {
            uint16_t value = gas_sensor_app_get_value(type);
            uint8_t unit = gas_sensor_app_get_unit(type);
            log_info(TAG, "%-4s: %u (unit=0x%02X)", s_names[i].name, value, unit);
        } else {
            log_warn(TAG, "%-4s: disconnected/no valid frame yet", s_names[i].name);
        }
    }
}