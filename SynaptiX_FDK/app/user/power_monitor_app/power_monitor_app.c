#include "power_monitor_app.h"
#include "logger.h"

static const char *TAG = "POWER_MONITOR_APP";

void power_monitor_app_init(power_monitor_app_t *app, ADS1115_T *ads1115)
{
    app->ads1115             = ads1115;
    app->period_elapsed_ms   = 0;
    app->next_channel        = POWER_MONITOR_APP_CH_CURRENT;
    app->last_current_a      = 0.0f;
    app->last_rail_voltage_v = 0.0f;
    app->has_current_reading = false;
    app->has_voltage_reading = false;
    app->last_status         = ADS1115_OK;
}

void power_monitor_app_poll(power_monitor_app_t *app, uint32_t delta_ms)
{
    app->period_elapsed_ms += delta_ms;
    if (app->period_elapsed_ms < POWER_MONITOR_APP_SAMPLE_PERIOD_MS) {
        return;
    }
    app->period_elapsed_ms = 0;

    float raw_voltage = 0.0f;

    if (app->next_channel == POWER_MONITOR_APP_CH_CURRENT) {
        app->last_status = ADS1115_readSingleEnded(app->ads1115, ADS1115_MUX_AIN1, &raw_voltage);
        if (app->last_status == ADS1115_OK) {
            /* ADS1115_readSingleEnded() returns millivolts (voltCoef is
             * mV/LSB — see ads1115.h's PGA comments, e.g. "0.0625 mV by
             * one bit" for PGA_TWO), so convert to volts before applying
             * Vout_AIN1 = INA180_GAIN * I * SHUNT_OHM -> I = Vout / (GAIN * SHUNT_OHM). */
            float vout_v = raw_voltage / 1000.0f;
            app->last_current_a = vout_v /
                (POWER_MONITOR_APP_INA180_GAIN * POWER_MONITOR_APP_SHUNT_OHM);
            app->has_current_reading = true;
        } else {
            log_warn(TAG, "AIN1 (current) read failed, status=%d", app->last_status);
        }
        app->next_channel = POWER_MONITOR_APP_CH_VOLTAGE;
    } else {
        app->last_status = ADS1115_readSingleEnded(app->ads1115, ADS1115_MUX_AIN2, &raw_voltage);
        if (app->last_status == ADS1115_OK) {
            /* Same mV->V conversion as above, then
             * Vout_AIN2 = Vin * R15/(R14+R15) -> Vin = Vout * (R14+R15)/R15. */
            float vout_v = raw_voltage / 1000.0f;
            app->last_rail_voltage_v = vout_v *
                (POWER_MONITOR_APP_DIVIDER_R14_OHM + POWER_MONITOR_APP_DIVIDER_R15_OHM) /
                POWER_MONITOR_APP_DIVIDER_R15_OHM;
            app->has_voltage_reading = true;
        } else {
            log_warn(TAG, "AIN2 (voltage) read failed, status=%d", app->last_status);
        }
        app->next_channel = POWER_MONITOR_APP_CH_CURRENT;
    }
}

bool power_monitor_app_has_current_reading(power_monitor_app_t *app)
{
    return app->has_current_reading;
}

bool power_monitor_app_has_voltage_reading(power_monitor_app_t *app)
{
    return app->has_voltage_reading;
}

float power_monitor_app_get_current_a(power_monitor_app_t *app)
{
    return app->last_current_a;
}

float power_monitor_app_get_rail_voltage_v(power_monitor_app_t *app)
{
    return app->last_rail_voltage_v;
}