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
 *  - Payload (2026-08-05, FINAL - per the user, deliberately dropping both
 *    "version" and "size" from an earlier draft of this design):
 *        {"url": "https://...", "crc32": "0x..."}
 *    crc32 (hex or decimal string, auto-detected - see fota.c's
 *    parse_crc32_string()) does double duty as both "which build is this"
 *    (no separate version string needed - two different builds essentially
 *    never share a CRC32 by chance) AND the post-download integrity check
 *    (see below). There is no "size" field: fota_download_attempt() (Part
 *    2, fota.c) instead downloads up to the fixed FOTA_MAX_FIRMWARE_SIZE
 *    ceiling and stops at the first HTTP range that returns fewer bytes
 *    than requested (true end-of-file, per a7677s_http.h's
 *    a7677s_http_range_cb_t doc-comment on data_len possibly being
 *    shorter than the requested range).
 *
 *  - Since a retained message does NOT disappear after being read (it is
 *    redelivered on every fresh MQTT connect, including one that just
 *    successfully applied that exact update), fota_on_message() compares
 *    the incoming crc32 against the crc32 of the last image this device
 *    actually applied - persisted across the update's own
 *    NVIC_SystemReset() in a spare RTC/TAMP backup register (see
 *    app/user/ota_trigger/new_boot_backup_reg.h, indices 3-4 - fota.c's
 *    Part 1 header comment has the full reasoning, including why a
 *    validity-marker register is used alongside the crc32 register
 *    itself). A matching crc32 is treated as "nothing to do", not
 *    re-downloaded.
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
 * flash_define.h SECONDARY_APP_FLASH_SIZE). There is no server-declared
 * "size" field to check against anymore (see this file's design
 * doc-comment above) - fota_download_attempt() (fota.c) instead bounds
 * its own download loop by this constant directly and aborts if EOF is
 * never signaled before reaching it (see that function's doc-comment). */
#define FOTA_MAX_FIRMWARE_SIZE (60UL * 8192UL) /* 480KB */

/* Number of consecutive CRC32-mismatch download attempts before this
 * module gives up on the currently-pending crc32 and waits for the
 * server to publish a new retained message (i.e. does not retry forever
 * burning battery on a bad/corrupted URL). Resets to 0 whenever a NEW
 * crc32 (this design's stand-in for a version string - see this file's
 * design doc-comment above) is seen on the retained topic. */
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
 * {"url":"...","crc32":"0x..."} (see this file's design doc-comment
 * above for why there is no "version"/"size" field), compares crc32
 * against the last-successfully-applied crc32 persisted in a backup
 * register (fota.c) and against the currently-pending crc32 if any, and
 * if genuinely new, latches the pending-update fields in-memory (does
 * not download here - see fota_download()). */
void fota_on_message(const char *topic, const char *message);

/* True if fota_on_message() has latched a not-yet-applied crc32 this boot
 * that has not yet been downloaded (or is mid-retry after a CRC
 * mismatch). Call from app.c's APP_CYCLE_WAIT_PUBLISH handler, AFTER
 * telemetry has been queued for publish, to decide whether to call
 * fota_download() this cycle. Deliberately RAM-only (see fota.c) -
 * always false again after a fresh boot until the retained message is
 * re-read on the next successful MQTT connect (the separate
 * backup-register record of the *last applied* crc32, unlike this flag,
 * DOES persist across boots - see fota.c's Part 1 header comment - so a
 * reboot does not cause a false "is pending" for an update already
 * applied). */
bool fota_is_pending(void);

/* Performs one full download+write+verify attempt for the currently
 * pending crc32 (see fota_is_pending()): downloads the firmware from
 * the latched URL in FOTA_HTTP_CHUNK_SIZE-byte HTTP Range requests (see
 * fota.c), writes each chunk directly into the Secondary partition,
 * then re-reads the whole partition and checks CRC32 against the
 * latched checksum.
 *
 * On success: flips isUpgradeInProgress=false + isNewFirmwareAvailable
 * =true together, records this crc32 as "last applied" in a backup
 * register, and calls NVIC_SystemReset() - does not return.
 *
 * On failure (HTTP error, CRC mismatch): logs the failure, increments
 * the retry counter, and returns. After FOTA_MAX_RETRY_COUNT consecutive
 * failures for the same crc32, clears the pending flag entirely
 * (fota_is_pending() becomes false) so the caller does not keep retrying
 * forever - a future retained message (even the same crc32 re-published,
 * which resets the retry counter - see fota_on_message()) is needed to
 * try again.
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