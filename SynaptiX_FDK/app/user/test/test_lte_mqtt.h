#ifndef TEST_LTE_MQTT_H
#define TEST_LTE_MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for A7677S modem + MQTT client, going through
 * the real component/service layers (sx_board.c's board.modem/a7677s,
 * app/user/sx_mqtt/sx_user_mqtt.c) — unlike the earlier pure-HAL tests
 * for SIM/GPS/IMU/Flash in Core/Src/main.c, this one deliberately reuses
 * the driver stack, since MQTT has no meaning below that layer.
 *
 * Deliberately bypasses network_config (see discussion with user,
 * 2026-07-28): app_config.h's USE_THINGSBOARD==1 branch currently makes
 * network_config's build_defaults() fall back to placeholder
 * "REPLACE_ME_BROKER_HOST"/"REPLACE_ME_CLIENT_ID" values, which is not
 * useful for a hardware bring-up test. Uses MQTT_HOST_TEST/
 * MQTT_CLIENTID_TEST (app_config.h) — a public broker (broker.hivemq.com)
 * — instead, hardcoded here, independent of flash-stored config.
 *
 * Call test_lte_mqtt_init() once after sx_board_init() (Core/Src/main.c),
 * then test_lte_mqtt_poll(delta_ms) every tick in the main while(1) loop,
 * same pattern as sx_user_mqtt_poll() itself expects. */

void test_lte_mqtt_init(void);
void test_lte_mqtt_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif