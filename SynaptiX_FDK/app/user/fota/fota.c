#ifndef FOTA_H
#define FOTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Network FOTA (Firmware Over-The-Air over the cellular modem), alongside
 * the existing USB DFU path (app/user/ota_trigger/) which stays untouched
 * and fully independent.
 *
 * Design (confirmed with the user across this session):
 *
 *  - The device is asleep (modem fully powered off) most of the time, so
 *    a server-initiated MQTT "push" RPC would be lost whenever it arrives
 *    during sleep (mqtt_clean_session=1 in this codebase - confirmed by
 *    reading mqtt_rpc.c/sx_user_mqtt usage - means the broker drops any
 *    message published while this client is disconnected). Instead, the
 *    device "pulls": the server publishes a RETAINED message once on
 *    FOTA_CHECK_TOPIC_PREFIX + device_id, and the device re-reads whatever
 *    is currently retained there every time it wakes and connects - a
 *    retained message survives regardless of how long the subscriber was
 *    offline, no persistent session needed.
 *
 *  - Detection and download are deliberately split across two wake
 *    cycles: the cycle that first sees a newer version only sets
 *    fota_is_pending() = true (RAM only, intentionally not persisted -
 *    the modem is fully powered off during sleep so nothing needs to
 *    survive a power cycle, only the following WAKING). The NEXT cycle,
 *    after telemetry has already been published, performs the actual
 *    download. This means a download failure never risks delaying or
 *    corrupting that cycle's sensor telemetry publish.
 *
 *  - Firmware is written directly into BOOTLOADER_WS's Secondary
 *    partition (0x08088000, 480KB, see ota_trigger.c's PARTITION_LAYOUT
 *    comment for the full internal-flash layout) via sx_flash_*(), NOT
 *    via USB DFU - this module owns the entire download+write+verify
 *    sequence itself, since there is no DFU host on the other end to
 *    checksum things for us.
 *
 *  - Only AFTER a full CRC32 match against the server-provided checksum
 *    does this module flip isUpgradeInProgress=false AND
 *    isNewFirmwareAvailable=true together in one read-modify-write (see
 *    fota.c's fota_trigger_swap_and_reset() - mirrors ota_trigger.c's
 *    pattern but must set BOTH flags, unlike ota_trigger_enter_dfu()
 *    which only sets isUpgradeInProgress because the DFU manifest
 *    callback on the bootloader side, tud_dfu_manifest_cb() in
 *    BOOTLOADER_WS/Core/Src/main.c, is the one that later sets
 *    isNewFirmwareAvailable - there is no such second actor here).
 *    Flags are deliberately NOT set before/during the download: if power
 *    is lost mid-download, isNewFirmwareAvailable stays false, so
 *    bootloader_process() falls through to its normal jump-to-Primary
 *    path on the next boot (confirmed by reading bootloader.c) instead
 *    of getting stuck waiting for a USB DFU session that will never
 *    come. The half-written Secondary is simply overwritten from
 *    scratch on the next retry - it is never read by anything until
 *    isNewFirmwareAvailable is true.
 *
 *  - BOOTLOADER_WS itself needs NO changes for any of this - confirmed
 *    by reading bootloader_process()/bootloader_init()/boot_swap_firmware()
 *    directly. It only ever reads isUpgradeInProgress/isNewFirmwareAvailable
 *    and does not care which actor (USB DFU manifest callback, or this
 *    module) set them.
 *
 * There is no polling function here beyond fota_check()/fota_download()
 * below, explicitly called from app.c's APP_CYCLE_WAIT_PUBLISH ->
 * APP_CYCLE_SLEEPING transition (see app.c) - this module does not drive
 * itself from a timer or MQTT callback alone. */

/* Topic prefix; actual topic is "<prefix><device_id>", same convention as
 * mqtt_rpc.c's build_rpc_request_topic(). Chosen to match RPC_REQUEST_API/
 * RPC_RESPONSE_API's "synaptix/demo/..." style already in app_config.h,
 * per the user's explicit choice - NOT MQTT_STATION_DATA_TOPIC's
 * "hanoi/air_quality/..." style, which is a different, older convention
 * used only for telemetry. */
#define FOTA_CHECK_TOPIC_PREFIX "synaptix/demo/fota_check/"

/* Hard ceiling = Secondary partition size (60 sectors * 8KB, see
 * ota_trigger.c's PARTITION_LAYOUT comment / BOOTLOADER_WS/bootloader/
 * flash_define.h SECONDARY_APP_FLASH_SIZE). A retained message
 * advertising a larger "size" is rejected outright before any download
 * starts - see fota.c's fota_check(). */
#define FOTA_MAX_FIRMWARE_SIZE (60UL * 8192UL) /* 480KB */

/* Number of consecutive CRC32-mismatch download attempts before this
 * module gives up on the currently-pending version and waits for the
 * server to publish a new retained message (i.e. does not retry forever
 * burning battery on a bad/corrupted URL). Resets to 0 whenever a NEW
 * version string is seen on the retained topic. */
#define FOTA_MAX_RETRY_COUNT 3

/* Subscribes to FOTA_CHECK_TOPIC_PREFIX + device_id. Matches
 * sx_user_mqtt_on_connected_cb_t. Called from app.c's dispatcher
 * alongside mqtt_rpc_init() - NOT wired directly as mqtt_cfg.on_connected,
 * since that slot is already taken by mqtt_rpc_init() (see app.c). */
void fota_init(void);

/* sx_user_mqtt_cfg_t's on_message callback shape. Called from app.c's
 * dispatcher alongside mqtt_rpc_on_message() - filters for topic ==
 * FOTA_CHECK_TOPIC_PREFIX + device_id itself, ignores everything else,
 * same pattern as mqtt_rpc_on_message(). Parses the retained payload
 * {"version":"x.y.z","url":"...","size":N,"crc32":"0x..."}, compares
 * version against APP_FW_VERSION (app_config.h) with a semver-aware
 * comparison (NOT strcmp - "0.2.0" vs "0.10.0" would compare wrong as
 * plain strings), and if newer, latches the pending-update fields
 * in-memory (does not download here - see fota_download()). */
void fota_on_message(const char *topic, const char *message);

/* True if fota_on_message() has latched a newer version this boot that
 * has not yet been downloaded (or is mid-retry after a CRC mismatch).
 * Call from app.c's APP_CYCLE_WAIT_PUBLISH handler, AFTER telemetry has
 * been queued for publish, to decide whether to call fota_download()
 * this cycle. Deliberately RAM-only (see fota.c) - always false again
 * after a fresh boot until the retained message is re-read on the next
 * successful MQTT connect. */
bool fota_is_pending(void);

/* Performs one full download+write+verify attempt for the currently
 * pending version (see fota_is_pending()): downloads the firmware from
 * the latched URL in FOTA_HTTP_CHUNK_SIZE-byte HTTP Range requests (see
 * fota.c), writes each chunk directly into the Secondary partition,
 * then re-reads the whole partition and checks CRC32 against the
 * latched checksum.
 *
 * On success: flips isUpgradeInProgress=false + isNewFirmwareAvailable
 * =true together and calls NVIC_SystemReset() - does not return.
 *
 * On failure (HTTP error, size mismatch, CRC mismatch): logs the
 * failure, increments the retry counter, and returns. After
 * FOTA_MAX_RETRY_COUNT consecutive failures for the same version,
 * clears the pending flag entirely (fota_is_pending() becomes false)
 * so the caller does not keep retrying forever - a future retained
 * message (even the same version re-published, which resets the retry
 * counter - see fota_on_message()) is needed to try again.
 *
 * Blocking - the modem's AT+HTTPACTION alone can take up to 120s per
 * range per A76XX AT command manual's MaxResponseTime; this is called
 * from app.c's APP_CYCLE_WAIT_PUBLISH -> APP_CYCLE_SLEEPING transition,
 * a point already tolerant of multi-second modem operations (see
 * APP_WAIT_PUBLISH_TIMEOUT_MS in app.c), but a full-size download across
 * many ranges will take substantially longer than that timeout - this is
 * a deliberate one-off exception to that timeout, not a violation of it,
 * since fota_download() is only entered when fota_is_pending() is true
 * (rare, one extra cycle after a real update is published), not every
 * cycle. */
void fota_download(void);

#ifdef __cplusplus
}
#endif

#endif // FOTA_H