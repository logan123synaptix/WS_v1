#include "test_ze12a.h"
#include "gas_sensor_app.h"
#include "ze12a.h"
#include "logger.h"

static const char *TAG = "TEST_ZE12A";

static uint32_t s_log_accum_ms = 0;
#define ZE12A_LOG_PERIOD_MS  2500U

/* gas_sensor_init() was already called inside sx_board_init() — it owns
 * its UART5 instance and both mux-select GPIOs internally (see ze12a.c
 * and sx_board.c comments). This test does not re-init anything, it
 * only drives the poll and reports gas_sensor_app's lookup API — same
 * "do not re-init what board init already did" pattern as
 * test_sht3x.c/test_imu.c. */
void test_ze12a_init(void)
{
    log_info(TAG, "=== TEST ZE12A (UART5 shared, mux round-robin, %u sensors) ===",
              GAS_SENSOR_COUNT);
    log_info(TAG, "Dwell %u ms/channel, timeout %u ms — allow at least %lu ms "
              "before expecting all channels connected",
              (unsigned)GAS_SENSOR_CHANNEL_DWELL_MS, (unsigned)GAS_SENSOR_TIMEOUT_MS,
              (unsigned long)(GAS_SENSOR_MUX_CHANNEL_COUNT * GAS_SENSOR_CHANNEL_DWELL_MS));
}

/* Static list so the log loop below can report a stable name per type
 * without duplicating the enum's raw hex values inline (matches the
 * GasSensorType_t enum in ze12a.h). Address 3 on the mux is deliberately
 * unpopulated on this board revision (see ze12a.h comment) and is not
 * one of the 5 named types tracked in gas_sensor[]. */
typedef struct {
    GasSensorType_t type;
    const char     *name;
} gas_sensor_name_t;

static const gas_sensor_name_t s_names[GAS_SENSOR_COUNT] = {
    { GAS_SENSOR_CO,  "CO"  },
    { GAS_SENSOR_SO2, "SO2" },
    { GAS_SENSOR_NO2, "NO2" },
    { GAS_SENSOR_O3,  "O3"  },
    { GAS_SENSOR_H2S, "H2S" },
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