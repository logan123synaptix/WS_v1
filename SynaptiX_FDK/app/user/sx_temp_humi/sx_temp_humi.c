#include "sx_temp_humi.h"
#include "logger.h"

static const char *TAG = "TEMP_HUMI";

void sx_temp_humi_init(sx_temp_humi_t *th, SHT3X_T *sht3x)
{
    th->sht3x              = sht3x;
    th->state              = SX_TEMP_HUMI_STATE_IDLE;
    th->period_elapsed_ms  = 0;
    th->measure_elapsed_ms = 0;
    th->has_reading        = false;
    th->last_status        = SHT3X_OK;
}

void sx_temp_humi_poll(sx_temp_humi_t *th, uint32_t delta_ms)
{
    switch (th->state) {
    case SX_TEMP_HUMI_STATE_IDLE:
        th->period_elapsed_ms += delta_ms;
        if (th->period_elapsed_ms < SX_TEMP_HUMI_SAMPLE_PERIOD_MS) {
            break;
        }
        th->period_elapsed_ms = 0;

        /* Kick off one single-shot measurement. Only sends the command
         * byte — sht3x_measure_single_shot() does not wait for or read
         * the result itself. */
        th->last_status = sht3x_measure_single_shot(th->sht3x, SX_TEMP_HUMI_MEASURE_CMD);
        if (th->last_status != SHT3X_OK) {
            log_warn(TAG, "measure cmd failed (%d), will retry next period", th->last_status);
            break; /* stay IDLE, try again next period rather than reading garbage early */
        }

        th->measure_elapsed_ms = 0;
        th->state = SX_TEMP_HUMI_STATE_MEASURING;
        break;

    case SX_TEMP_HUMI_STATE_MEASURING:
        th->measure_elapsed_ms += delta_ms;
        if (th->measure_elapsed_ms < SX_TEMP_HUMI_MEASURE_WAIT_MS) {
            break;
        }

        th->last_status = sht3x_read_measurement(th->sht3x);
        if (th->last_status == SHT3X_OK) {
            th->has_reading = true;
            log_debug(TAG, "T=%.2fC RH=%.2f%%", th->sht3x->temperature, th->sht3x->humidity);
        } else {
            log_warn(TAG, "read failed (%d)", th->last_status);
        }

        th->state = SX_TEMP_HUMI_STATE_IDLE;
        break;

    default:
        th->state = SX_TEMP_HUMI_STATE_IDLE;
        break;
    }
}

bool sx_temp_humi_is_ready(sx_temp_humi_t *th)
{
    return th->has_reading;
}

float sx_temp_humi_get_temperature(sx_temp_humi_t *th)
{
    return th->sht3x->temperature;
}

float sx_temp_humi_get_humidity(sx_temp_humi_t *th)
{
    return th->sht3x->humidity;
}