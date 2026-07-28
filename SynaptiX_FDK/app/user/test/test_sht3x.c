#include "test_sht3x.h"
#include "sx_board.h"
#include "sx_temp_humi.h"
#include "logger.h"

static const char *TAG = "TEST_SHT3X";

/* Owned here, not in Board_t — sx_temp_humi_t is just the polling
 * cadence wrapper around board.sht3x (see sx_temp_humi.h), it doesn't
 * need to live on the board struct for a standalone test. */
static sx_temp_humi_t s_th;

void test_sht3x_init(void)
{
    log_info(TAG, "=== TEST SHT3X (I2C1, addr=0x%02X) ===", SHT3X_DEVICE_ADDR >> 1);

    /* board.sht3x was already sht3x_init()'d inside sx_board_init()
     * (sx_board.c) — do NOT call sht3x_init() again here, it would
     * re-send a soft reset for no reason and mask whether board init
     * itself succeeded. This wrapper only starts the periodic
     * single-shot cadence on top of the already-initialized sensor. */
    sx_temp_humi_init(&s_th, &board.sht3x);

    /* Read status register once at boot as an explicit presence/comms
     * check independent of the periodic cadence below — if the sensor
     * is not wired/powered correctly, this fails immediately instead
     * of silently retrying every SX_TEMP_HUMI_SAMPLE_PERIOD_MS. */
    uint16_t status = 0;
    SHT3X_STATUS_T ret = sht3x_read_status(&board.sht3x, &status);
    if (ret == SHT3X_OK) {
        log_info(TAG, "Status register OK: 0x%04X", status);
    } else {
        log_error(TAG, "Status register read FAILED (ret=%d) — check I2C1 wiring/address", ret);
    }

    log_info(TAG, "Init done — sampling every %u ms",
             (unsigned)SX_TEMP_HUMI_SAMPLE_PERIOD_MS);
}

void test_sht3x_poll(uint32_t delta_ms)
{
    /* Drives the single-shot measure -> wait -> read state machine,
     * same call app.c's real sx_temp_humi_poll() makes every tick.
     * Capture pre-poll state to edge-detect a just-completed read
     * below (MEASURING -> IDLE transition), since has_reading only
     * ever latches true and stays true (see sx_temp_humi.c) and can't
     * by itself tell "new reading this tick" from "old reading". */
    sx_temp_humi_state_t state_before = s_th.state;
    sx_temp_humi_poll(&s_th, delta_ms);

    /* sx_temp_humi_poll() already logs at DEBUG level on every
     * successful read (see sx_temp_humi.c) — this INFO-level log here
     * is deliberately separate: it's the "for this bring-up test"
     * summary line, visible even if the log level filters out DEBUG,
     * and it fires exactly once per completed read cycle. */
    if (state_before == SX_TEMP_HUMI_STATE_MEASURING &&
        s_th.state == SX_TEMP_HUMI_STATE_IDLE) {
        if (s_th.last_status == SHT3X_OK) {
            log_info(TAG, "T=%.2f C  RH=%.2f %%",
                     sx_temp_humi_get_temperature(&s_th),
                     sx_temp_humi_get_humidity(&s_th));
        } else {
            log_error(TAG, "Read FAILED (status=%d)", s_th.last_status);
        }
    }
}