#include "test_sps30.h"
#include "sx_board.h"
#include "sps30_app.h"
#include "logger.h"

static const char *TAG = "TEST_SPS30";

/* Owned here, not in Board_t — sps30_app_t is the app-layer state
 * machine (POWER_ON -> WAKE_UP -> ... -> SLEEP) built on top of the
 * SHDLC command API in sps30_uart.c (see sps30_app.h), same pattern as
 * accel_app_t/sx_temp_humi_t in the other tests. */
static sps30_app_t s_sps30;

/* Delay between one cycle finishing (DONE) and starting the next, so
 * this test's log output is readable and mirrors app.c's real cadence
 * (ON_PUMP/SENSING/SENDING/SLEEPING loop) instead of hammering the
 * sensor back-to-back. Not tied to the real app_config.h timing values
 * on purpose — this is a standalone bring-up test, not the production
 * cycle. */
#define TEST_SPS30_CYCLE_GAP_MS  5000U

static uint32_t s_gap_elapsed_ms = 0;
static bool s_waiting_for_next_cycle = false;

void test_sps30_init(void)
{
    log_info(TAG, "=== TEST SPS30 (UART4, SHDLC, EN_PW_DUST power-gated) ===");

    /* sx_board_get_sps30_power_gpio() returns board's already-init'd
     * EN_PW_DUST GPIO (sx_gpio_init() done inside sx_board_init()) —
     * sps30_app_init() only stores the pointer, it does not configure
     * the GPIO itself. board.sps30_uart was likewise already
     * sx_uart_init()'d + sensirion_uart_hal_init()'d in sx_board_init(). */
    sps30_app_init(&s_sps30, sx_board_get_sps30_power_gpio());

    log_info(TAG, "Starting first measurement cycle...");
    sps30_app_start_cycle(&s_sps30);
}

void test_sps30_poll(uint32_t delta_ms)
{
    if (s_waiting_for_next_cycle) {
        s_gap_elapsed_ms += delta_ms;
        if (s_gap_elapsed_ms >= TEST_SPS30_CYCLE_GAP_MS) {
            s_gap_elapsed_ms = 0;
            s_waiting_for_next_cycle = false;
            log_info(TAG, "Starting next measurement cycle...");
            sps30_app_start_cycle(&s_sps30);
        }
        return;
    }

    /* Drives the POWER_ON -> WAKE_UP -> START_MEASUREMENT ->
     * WAIT_MEASUREMENT -> READ_MEASUREMENT -> STOP_MEASUREMENT state
     * machine, same call app.c's real sps30_app_poll() makes every tick. */
    sps30_app_poll(&s_sps30, delta_ms);

    if (sps30_app_is_cycle_done(&s_sps30)) {
        if (sps30_app_has_measurement(&s_sps30)) {
            const sps30_app_measurement_t *m = sps30_app_get_measurement(&s_sps30);
            log_info(TAG, "PM1.0=%.2f PM2.5=%.2f PM4.0=%.2f PM10=%.2f ug/m3, typical size=%.2f um",
                      m->mc_1p0, m->mc_2p5, m->mc_4p0, m->mc_10p0, m->typical_particle_size);
        } else {
            log_error(TAG, "Cycle DONE but no measurement captured — check UART4 wiring/SHDLC comms");
        }

        /* TEMP DEBUG CHANGE (per user request): keep EN_PW_DUST HIGH
         * continuously instead of power-cycling between cycles — no
         * sps30_app_sleep_step_start() call here anymore, so the
         * opto+MOSTFET power path never gets a falling edge between
         * cycles. Goal: isolate whether the earlier "wake_up_sequence
         * failed, err=-2" was caused by the sensor not having settled
         * yet right after each fresh power-on (1000ms window), or by
         * an unrelated hardware/wiring fault -- if leaving power on
         * continuously lets it read successfully, that points at
         * settle timing / the power path's rise time, not UART4 wiring
         * or the SHDLC command sequence itself.
         * sps30_app_reset() still resets the state machine to IDLE (so
         * the next sps30_app_start_cycle() below can run), but
         * start_cycle() unconditionally re-drives EN_PW_DUST high again
         * -- harmless here since it is already high, no falling edge is
         * produced. Revert to calling sps30_app_sleep_step_start()
         * before reset once done debugging, to restore the normal
         * "power off between test cycles" behavior. */
        sps30_app_reset(&s_sps30);
        s_waiting_for_next_cycle = true;
        s_gap_elapsed_ms = 0;
    }
}