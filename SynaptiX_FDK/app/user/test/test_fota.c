#include "test_fota.h"
#include "sx_board.h"
#include "sx_user_mqtt.h"
#include "fota.h"
#include "network_config.h"
#include "app_config.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "TEST_FOTA";

typedef enum {
    TEST_FOTA_MQTT_INIT = 0,
    TEST_FOTA_WAIT_CONNECT,
    TEST_FOTA_WAIT_PENDING,   /* subscribed, waiting for a retained message to latch a pending update */
    TEST_FOTA_DOWNLOADING,    /* fota_download() called, one attempt in flight (blocking call itself -
                                * see below, this state exists only for the log line bracketing it,
                                * fota_download() does not return until the attempt is fully done) */
    TEST_FOTA_DONE,           /* fota_download() returned without resetting - see on-screen log for
                                * why (retry give-up, or a failure this test does not retry itself -
                                * matches app.c's real intended "one attempt per wake cycle" usage,
                                * this test does not loop fota_download() calls on its own) */
} test_fota_state_t;

static test_fota_state_t s_state = TEST_FOTA_MQTT_INIT;
static uint8_t s_was_connected = 0; /* edge-detect for connect/disconnect log, same as test_lte_mqtt.c */
static bool s_fota_init_called = false; /* fota_init() (subscribe) must only run once per connect,
                                          * matching mqtt_rpc_init()'s real on_connected contract -
                                          * see app.c's cfg.on_connected wiring for the pattern this
                                          * test mirrors by hand since there is no app.c dispatcher here */

/* --- sx_user_mqtt_cfg_t callbacks --- */

static void on_connected(void)
{
    log_info(TAG, "MQTT connected callback fired");
    /* fota_init() itself just subscribes - safe to call here directly,
     * same as app.c would via its on_connected dispatcher (see app.c's
     * real wiring: mqtt_cfg.on_connected = mqtt_rpc_init, with fota_init()
     * intended to sit alongside it once wired - this test calls it here
     * by hand since that real wiring does not exist yet, per the user's
     * explicit "test first, wire into app.c only if this passes" request). */
    fota_init();
    s_fota_init_called = true;
}

static void on_disconnected(void)
{
    log_warn(TAG, "MQTT disconnected callback fired");
}

static void on_message(const char *topic, const char *message)
{
    log_info(TAG, "MSG RX [%s] = %s", topic, message);
    /* fota_on_message() itself filters for its own expected topic and
     * ignores anything else - same as app.c's real dispatcher would call
     * it unconditionally alongside mqtt_rpc_on_message() for every
     * inbound message, not just ones this test already knows are FOTA
     * related. */
    fota_on_message(topic, message);
}

static void on_publish(int success)
{
    (void)success; /* this test never publishes anything itself - present only
                     * because sx_user_mqtt_cfg_t requires all four callbacks */
}

/* Mirrors fota.c's own build_fota_check_topic() (static, not exported) so
 * this test can print the exact topic to publish to without needing that
 * function made non-static just for a test file - same
 * "<prefix><device_id>" convention as fota.h/fota.c, computed the same
 * way (network_config_get()->device_id, read fresh, not cached) so this
 * always matches what fota_on_message() itself will actually filter on,
 * even if device_id was changed via CLI/RPC before this test runs. */
static void print_fota_topic(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s", FOTA_CHECK_TOPIC_PREFIX, network_config_get()->device_id);
}

void test_fota_init(void)
{
    log_info(TAG, "=== TEST FOTA (fota.c end-to-end bring-up, real MQTT + real HTTP "
                   "range download + real flash write/verify/swap) ===");
    log_info(TAG, "WARNING: on a CRC32 match this test WILL erase+write the Secondary "
                   "partition and WILL reset the board to swap into whatever image was "
                   "downloaded - confirmed acceptable by the user for this test run.");

    /* BUG FIX (2026-08-05, confirmed on real hardware log): this test (and
     * fota.c itself, via network_config_get()->device_id in
     * build_fota_check_topic()) reads network_config_get()->device_id, but
     * nothing had called network_config_init() first anywhere in this
     * test's own call chain - the real app.c flow calls it (see app.c's
     * app_init()), but that path is not exercised by this standalone test.
     * s_cfg (network_config.c) is a plain static struct, zero-initialized
     * by the C runtime, NOT pre-filled with DEVICE_ID's "001" default until
     * network_config_init() actually runs (build_defaults() inside it) -
     * without this call, device_id was an empty string, so this test
     * subscribed to "synaptix/demo/fota_check/" (prefix only, no id) instead
     * of ".../001", confirmed by the real subscribe-ack log line. Safe/cheap
     * to call here: reads from flash storage if a config was previously
     * saved there, otherwise falls back to app_config.h's DEVICE_ID default
     * and writes it once - same call app.c's app_init() makes, just made
     * explicitly here since this test does not go through app_init() at
     * all. */
    network_config_init();

    sx_user_mqtt_cfg_t cfg = {0};
    cfg.broker          = MQTT_HOST_TEST;
    cfg.port            = 1883;
    cfg.client_id       = MQTT_CLIENTID_TEST;
    cfg.username        = NULL;
    cfg.password        = NULL;
    cfg.keepalive       = 60;
    cfg.clean_session   = 1;
    cfg.use_ssl         = 0;
    cfg.on_connected    = on_connected;
    cfg.on_disconnected = on_disconnected;
    cfg.on_message      = on_message;
    cfg.on_publish      = on_publish;

    int ret = sx_user_mqtt_nontls_init(&cfg);
    if (ret != 0) {
        log_error(TAG, "sx_user_mqtt_nontls_init FAILED (ret=%d)", ret);
        return;
    }

    s_state           = TEST_FOTA_WAIT_CONNECT;
    s_was_connected    = 0;
    s_fota_init_called = false;

    log_info(TAG, "Init OK - waiting for modem power-on + network + MQTT handshake...");
}

void test_fota_poll(uint32_t delta_ms)
{
    /* Drives modem power-on, network registration, and MQTT connect/
     * publish/subscribe state machines - same call app.c's real
     * app_process() makes every tick (see test_lte_mqtt.c's identical
     * call for the same reason). This alone is enough to also drive
     * a7677s_http.c's AT-command-based steps (SSL configure, HTTPACTION)
     * during fota_download() below, since those go through the same
     * modem_handle_poll() chain - it is ONLY a7677s_http_poll() (the
     * separate raw-UART HTTPREAD reader) that additionally needs its own
     * call, which fota.c's fota_wait_done() already makes internally
     * during fota_download() - this test does not need to call
     * a7677s_http_poll() itself, unlike test_http.c, because it never
     * calls a7677s_http_get_range()/a7677s_http_ssl_configure() directly -
     * fota_download() does that on this test's behalf. */
    sx_user_mqtt_poll(delta_ms);

    uint8_t connected = sx_user_mqtt_is_connected();
    if (connected && !s_was_connected) {
        log_info(TAG, "-> Now CONNECTED. IP=%s IMEI=%s RSSI=%d operator=%s",
                 sx_user_mqtt_get_ip(), sx_user_mqtt_get_imei(),
                 sx_user_mqtt_get_rssi(), sx_user_mqtt_get_operator());
    } else if (!connected && s_was_connected) {
        log_warn(TAG, "-> Now DISCONNECTED");
    }
    s_was_connected = connected;

    switch (s_state) {
    case TEST_FOTA_MQTT_INIT:
        return; /* test_fota_init() has not been called, or its init failed - nothing to do */

    case TEST_FOTA_WAIT_CONNECT:
        if (!connected) {
            return; /* keep waiting, poll() will be called again next tick */
        }
        /* on_connected() (above) already called fota_init() by the time
         * sx_user_mqtt_is_connected() flips true (same callback-before-
         * flag-visible ordering test_lte_mqtt.c already relies on for its
         * own "-> Now CONNECTED" log line above) - s_fota_init_called is
         * asserted here only as a safety check against that ordering
         * assumption ever being wrong, not as the actual trigger for
         * calling fota_init(). */
        if (!s_fota_init_called) {
            log_error(TAG, "Connected but fota_init() was not called via on_connected() - "
                            "ordering assumption broken, aborting test");
            s_state = TEST_FOTA_DONE;
            return;
        }
        {
            char topic[64];
            print_fota_topic(topic, sizeof(topic));
            log_info(TAG, "MQTT connected, subscribed to %s - waiting for a retained "
                           "FOTA check message... (publish {\"url\":\"...\",\"crc32\":\"0x...\"} "
                           "RETAINED to this exact topic via MQTT Explorer)", topic);
        }
        s_state = TEST_FOTA_WAIT_PENDING;
        return;

    case TEST_FOTA_WAIT_PENDING:
        if (!fota_is_pending()) {
            return; /* keep waiting - on_message() -> fota_on_message() will flip this
                      * true once a valid retained message arrives, see that function's log */
        }
        log_info(TAG, "fota_is_pending() is now true - calling fota_download() "
                       "(this call blocks until the attempt is fully done, see fota.h)");
        s_state = TEST_FOTA_DOWNLOADING;
        fota_download();
        /* fota_download() either:
         *   - does not return at all (CRC32 matched, swap+NVIC_SystemReset()
         *     already fired - see fota_trigger_swap_and_reset()), or
         *   - returns having failed this attempt (logged in detail already
         *     by fota_download()/fota_download_attempt() themselves - see
         *     fota.c) and incremented/checked the retry counter.
         * Either way, nothing further to do here on this tick - fall
         * through to DONE so this test does not immediately call
         * fota_download() again on the very next tick (matching the real
         * intended "one attempt per wake cycle" usage this test is meant
         * to confirm, not a tight retry loop). */
        s_state = TEST_FOTA_DONE;
        log_info(TAG, "=== TEST FOTA: fota_download() attempt returned (did not reset) - "
                       "see log above for pass/fail detail. pending=%d retry logic is "
                       "internal to fota.c, not re-driven by this test ===",
                 (int)fota_is_pending());
        return;

    case TEST_FOTA_DOWNLOADING:
        return; /* unreachable in practice - fota_download() above is blocking and this
                  * test moves straight to DONE right after it returns, on the same tick */

    case TEST_FOTA_DONE:
        return; /* test finished (or gave up) - see log for the outcome, nothing more to do */
    }
}