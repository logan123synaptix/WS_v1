#include "test_ads1115.h"
#include "sx_board.h"
#include "power_monitor_app.h"
#include "logger.h"

static const char *TAG = "TEST_ADS1115";

/* Owned here, not on Board_t — power_monitor_app_t is just the
 * alternating-channel polling cadence wrapper around board.ads1115
 * (see power_monitor_app.h), it doesn't need to live on the board
 * struct for a standalone test. */
static power_monitor_app_t s_pm;

/* Log cadence for this test's own summary line — independent of
 * POWER_MONITOR_APP_SAMPLE_PERIOD_MS (1000ms per channel, i.e. ~2000ms
 * for a full AIN1+AIN2 cycle), just needs to be slow enough that both
 * channels have usually updated at least once between prints. */
#define TEST_ADS1115_LOG_PERIOD_MS 2500U

static uint32_t s_log_accum_ms = 0;

void test_ads1115_init(void)
{
    log_info(TAG, "=== TEST ADS1115 (I2C1, addr=0x%02X) ===", ADS1115_DEVICE_ADDR >> 1);
    log_info(TAG, "R16 shunt = %.3f ohm, INA180 gain = %.1f V/V -> full-scale current at PGA_TWO FSR (2.048V) = %.2f A",
              (double)POWER_MONITOR_APP_SHUNT_OHM,
              (double)POWER_MONITOR_APP_INA180_GAIN,
              (double)(2.048f / (POWER_MONITOR_APP_INA180_GAIN * POWER_MONITOR_APP_SHUNT_OHM)));

    /* board.ads1115 was already ADS1115_Init()'d inside sx_board_init()
     * (sx_board.c) — do NOT call ADS1115_Init() again here, it would
     * just re-issue the same config write for no reason and mask
     * whether board init itself already succeeded. */
    power_monitor_app_init(&s_pm, &board.ads1115);

    log_info(TAG, "Init done — alternating AIN1(current)/AIN2(voltage), one channel every %u ms",
              (unsigned)POWER_MONITOR_APP_SAMPLE_PERIOD_MS);
}

void test_ads1115_poll(uint32_t delta_ms)
{
    /* Drives the alternating single-channel read cadence, same call
     * app.c's real power_monitor_app_poll() makes every tick. */
    power_monitor_app_poll(&s_pm, delta_ms);

    if (s_pm.last_status != ADS1115_OK) {
        /* power_monitor_app_poll() already logs a WARNING itself on a
         * failed channel read (tag=POWER_MONITOR_APP) — nothing to add
         * here beyond letting that propagate, avoid double-logging the
         * same failure under a second tag. */
        return;
    }

    s_log_accum_ms += delta_ms;
    if (s_log_accum_ms < TEST_ADS1115_LOG_PERIOD_MS) {
        return;
    }
    s_log_accum_ms = 0;

    bool have_i = power_monitor_app_has_current_reading(&s_pm);
    bool have_v = power_monitor_app_has_voltage_reading(&s_pm);

    if (!have_i && !have_v) {
        log_info(TAG, "No readings yet...");
        return;
    }

    if (have_i) {
        log_info(TAG, "AIN1 rail current: %.4f A", (double)power_monitor_app_get_current_a(&s_pm));
    } else {
        log_info(TAG, "AIN1 rail current: (no reading yet)");
    }

    if (have_v) {
        log_info(TAG, "AIN2 rail voltage: %.3f V", (double)power_monitor_app_get_rail_voltage_v(&s_pm));
    } else {
        log_info(TAG, "AIN2 rail voltage: (no reading yet)");
    }
}