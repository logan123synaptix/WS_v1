/* ===================== Part 1: init / on_message / is_pending ===================== */
/*
 * NOT YET TESTED ON REAL HARDWARE (2026-08-05) - unlike Part 2 below
 * (fota_download() and everything it calls), which was written and
 * reviewed against real HTTP range-download logs already, this Part 1
 * code has not been built or run yet. Flagged explicitly per this
 * project's own rule (see the handoff notes elsewhere in this codebase's
 * history: never claim something works without a real board confirming
 * it) - test this against a real retained MQTT message before trusting
 * it in the field.
 *
 * Payload design (2026-08-05, per the user - fota.h's doc-comment has
 * been updated to match this, both files now agree): the user
 * deliberately dropped BOTH "version" and "size" from an earlier draft.
 *     {"url": "...", "crc32": "0x..."}
 *
 * Why "version" was dropped and crc32 used as the "is this new" signal
 * instead (this was flagged as a real problem before writing this: a
 * retained MQTT message does NOT disappear after being read - it is
 * still there the NEXT time this device wakes, subscribes, and reads it
 * again. Without some way to recognize "I have already successfully
 * applied this exact firmware", the device would re-download and
 * re-flash the SAME firmware every single wake cycle forever, forever
 * burning battery/data on a no-op). crc32 already uniquely identifies
 * "which build" (two different builds essentially never share a CRC32
 * by chance) SO it doubles as both "is this a new image" AND "does what
 * I downloaded match what the server meant" - no separate version string
 * needed for either purpose.
 *
 * Where "last successfully applied crc32" is stored so it survives the
 * NVIC_SystemReset() that fota_trigger_swap_and_reset() (Part 2) always
 * calls on success (per the user, 2026-08-05: use a spare RTC/TAMP backup
 * register - see new_boot_backup_reg.h/.c under app/user/ota_trigger/,
 * already used by this same project for the unrelated "enter DFU"/
 * "rollback" one-shot flags, indices 0-2 - this reuses that same
 * 32-register file, NOT a second copy of the backup-register driver, at
 * previously-unused indices).
 *
 *   - FOTA_BACKUP_REG_LAST_APPLIED_CRC32 (index 3, see #define below)
 *     holds the raw crc32 value of the last image this module actually
 *     wrote+verified+swapped in. boot_backup_reg_read()/write() operate
 *     on plain TAMP->BKPxR registers (32-bit, no magic/validity marker of
 *     their own) - see that module's doc-comment: these survive a
 *     software reset (confirmed - that is their entire purpose in this
 *     codebase already, for the DFU/rollback flags). CONFIRMED by the
 *     user (2026-08-05): this board's STM32 VBAT pin is hardwired
 *     directly to the 3.3V rail (not a coin cell/supercap) - so the
 *     backup domain in fact survives ANY reset while the board has power
 *     at all (software reset, watchdog, NRST pin, brown-out recovery),
 *     not just NVIC_SystemReset() specifically. It is only lost on a true
 *     full power-down of the board (3.3V rail itself gone) - handled
 *     safely below regardless (see the validity-marker point next).
 *
 *   - Because a full power-down (not any kind of reset - see above) can
 *     still clear the backup domain, a second backup register
 *     (FOTA_BACKUP_REG_LAST_APPLIED_VALID, index 4) is used purely as
 *     a validity marker (written with a fixed magic value, immediately
 *     after a successful write of the crc32 register itself) - a
 *     power-loss that clears backup domain SRAM will clear BOTH
 *     registers back to 0, which reads as "not valid" via this marker,
 *     rather than being misread as "the last applied crc32 was literally
 *     0x00000000" (astronomically unlikely for a real CRC32 of a real
 *     firmware image, but not impossible, and cheap to rule out
 *     explicitly rather than rely on that being rare enough). Losing this
 *     memory after a real power cycle is not unsafe by itself - the worst
 *     case is one extra redundant download+flash+verify+reset of a
 *     firmware image that was already running, not a bricked device (the
 *     new image passes the exact same CRC32 check as any other
 *     legitimate update before it is ever swapped in).
 */

#include "fota.h"
#include "sx_flash.h"
#include "logger.h"
#include "sx_board.h"
#include "sx_user_mqtt.h"
#include "network_config.h"
#include "new_boot_backup_reg.h"
#include "a7677s_http.h"
#include "cJSON.h"
#include "stm32h5xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "FOTA";

/* See this file's Part 1 header comment above for the full reasoning.
 * Indices 0-2 are already taken by app/user/ota_trigger/new_boot_backup_reg.h
 * (BOOT_BACKUP_REG_UPDATE/ROLLBACK_PREV/ROLLBACK_FACTORY) - this module
 * claims the next two, 3 and 4, and must never reuse 0-2 or collide with
 * any future addition there (that header is the single source of truth
 * for which indices are taken; if it ever claims 3 or 4 for something
 * else, THIS file must move to different indices, not the other way
 * around, since ota_trigger.c's flags are read by the bootloader itself
 * and are more load-bearing than this module's own bookkeeping). */
#define FOTA_BACKUP_REG_LAST_APPLIED_CRC32   3U
#define FOTA_BACKUP_REG_LAST_APPLIED_VALID   4U
#define FOTA_BACKUP_REG_VALID_MAGIC          0xFA710001UL

/* Topic buffer sizing: FOTA_CHECK_TOPIC_PREFIX (fota.h) is longer than
 * mqtt_rpc.c's RPC_REQUEST_API/RPC_RESPONSE_API, so RPC_TOPIC_BUFF_SIZE's
 * value (64) is not blindly reused here - computed fresh against this
 * module's own actual prefix length + NETWORK_CONFIG_DEVICE_ID_MAX_LEN
 * (32, network_config.h), same margin style as mqtt_rpc.c. */
#define FOTA_TOPIC_BUFF_SIZE   64

/* URL max length mirrors a7677s_http.h's A7677S_HTTP_URL_MAX (257,
 * including NUL) - this module never passes a longer string down to
 * a7677s_http_get_range() anyway, so there is no point storing more than
 * that module could ever actually use. crc32 hex string from the JSON
 * payload (e.g. "0xDEADBEEF", up to 10 chars + NUL) is parsed into a raw
 * uint32_t immediately (see fota_on_message() below) and not kept as a
 * string past that point. */
typedef struct {
    bool     pending;                       /* true = a newer, not-yet-downloaded/verified image is latched */
    char     url[A7677S_HTTP_URL_MAX];
    uint32_t crc32;                         /* target checksum for the currently pending/in-progress attempt */
    uint32_t retry_count;                   /* consecutive failed fota_download() attempts for THIS crc32 */
} fota_state_t;

static fota_state_t s_fota = {0};

/* --- Backup-register helpers, local to this file --- */

static bool fota_backup_get_last_applied_crc32(uint32_t *out_crc32)
{
    boot_backup_reg_init();
    uint32_t valid_marker = boot_backup_reg_read(FOTA_BACKUP_REG_LAST_APPLIED_VALID);
    if (valid_marker != FOTA_BACKUP_REG_VALID_MAGIC) {
        return false; /* never written, or backup domain lost power - see file header comment */
    }
    *out_crc32 = boot_backup_reg_read(FOTA_BACKUP_REG_LAST_APPLIED_CRC32);
    return true;
}

static void fota_backup_set_last_applied_crc32(uint32_t crc32)
{
    boot_backup_reg_init();
    boot_backup_reg_write(FOTA_BACKUP_REG_LAST_APPLIED_CRC32, crc32);
    /* Valid marker written SECOND, after the crc32 value itself - so if
     * this device somehow lost power in between these two writes (should
     * be near-impossible, both are simple register writes with no delay
     * between them, but there is no hardware atomicity across two
     * separate 32-bit registers), the marker would still read as
     * "invalid" on the next boot rather than pairing a valid-looking
     * marker with a half-written/stale crc32 value from an earlier,
     * different image. */
    boot_backup_reg_write(FOTA_BACKUP_REG_LAST_APPLIED_VALID, FOTA_BACKUP_REG_VALID_MAGIC);
}

/* --- Topic helper, same shape as mqtt_rpc.c's build_rpc_request_topic() --- */

static void build_fota_check_topic(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s", FOTA_CHECK_TOPIC_PREFIX, network_config_get()->device_id);
}

/* --- Parse "0x..." or plain-decimal crc32 string from JSON into a raw uint32_t ---
 * strtoul() with base 0 auto-detects "0x"/"0X" prefix (hex) vs a plain
 * decimal string, so either representation the server happens to publish
 * works without this module needing to know which one in advance. Returns
 * false if the string parsed to nothing usable (empty, or no digits
 * consumed at all) - a malformed crc32 field must not silently become 0
 * and be treated as a real target checksum. */
static bool parse_crc32_string(const char *s, uint32_t *out)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }
    char *endptr = NULL;
    unsigned long val = strtoul(s, &endptr, 0);
    if (endptr == s) {
        return false; /* no digits consumed at all */
    }
    *out = (uint32_t)val;
    return true;
}

void fota_init(void)
{
    char topic[FOTA_TOPIC_BUFF_SIZE];
    build_fota_check_topic(topic, sizeof(topic));
    sx_user_mqtt_subscribe(topic);
    log_info(TAG, "Subscribed to %s", topic);
}

void fota_on_message(const char *topic, const char *message)
{
    char expected_topic[FOTA_TOPIC_BUFF_SIZE];
    build_fota_check_topic(expected_topic, sizeof(expected_topic));
    if (strcmp(topic, expected_topic) != 0) {
        return;
    }

    log_info(TAG, "FOTA check message received: %s", message);

    cJSON *root = cJSON_Parse(message);
    if (root == NULL) {
        log_error(TAG, "Failed to parse FOTA check JSON");
        return;
    }

    cJSON *url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
    if (!cJSON_IsString(url_item) || url_item->valuestring == NULL || url_item->valuestring[0] == '\0') {
        log_error(TAG, "FOTA check message missing/invalid \"url\"");
        cJSON_Delete(root);
        return;
    }

    cJSON *crc32_item = cJSON_GetObjectItemCaseSensitive(root, "crc32");
    uint32_t new_crc32 = 0;
    if (!cJSON_IsString(crc32_item) || !parse_crc32_string(crc32_item->valuestring, &new_crc32)) {
        log_error(TAG, "FOTA check message missing/invalid \"crc32\"");
        cJSON_Delete(root);
        return;
    }

    if (strlen(url_item->valuestring) >= sizeof(s_fota.url)) {
        log_error(TAG, "FOTA check message \"url\" too long (%u chars, max %u) - ignoring",
                   (unsigned)strlen(url_item->valuestring), (unsigned)sizeof(s_fota.url) - 1U);
        cJSON_Delete(root);
        return;
    }

    /* Same-crc32-as-last-successfully-applied check: this is what stands
     * in for "version" (see file header comment on why version was
     * dropped). A retained message describing the firmware ALREADY
     * running on this device must not trigger a re-download - it is not
     * "new", it is the same image the server published the last time
     * this device updated. */
    uint32_t last_applied_crc32;
    if (fota_backup_get_last_applied_crc32(&last_applied_crc32) && last_applied_crc32 == new_crc32) {
        log_info(TAG, "FOTA check: crc32=0x%08lX matches last-applied image, nothing to do",
                   (unsigned long)new_crc32);
        cJSON_Delete(root);
        return;
    }

    /* Same-crc32-as-currently-pending check: a retained message is
     * re-delivered on every fresh MQTT (re)connect, which can happen more
     * than once without an intervening fota_download() attempt (e.g. a
     * connection blip mid-cycle). Without this check, re-arriving at the
     * SAME still-pending crc32 would reset retry_count back to 0 every
     * time, defeating FOTA_MAX_RETRY_COUNT's whole purpose (a genuinely
     * failing download would then retry forever, one MQTT reconnect at a
     * time, instead of ever reaching the give-up threshold). Per fota.h's
     * doc-comment on FOTA_MAX_RETRY_COUNT, retry_count should only reset
     * when a truly NEW version string is seen - crc32 is now that
     * identity, so this is the direct translation of that original
     * intent to the new payload shape. */
    if (s_fota.pending && s_fota.crc32 == new_crc32) {
        log_info(TAG, "FOTA check: crc32=0x%08lX already pending (retry %lu/%lu), not resetting",
                   (unsigned long)new_crc32, (unsigned long)s_fota.retry_count,
                   (unsigned long)FOTA_MAX_RETRY_COUNT);
        cJSON_Delete(root);
        return;
    }

    strncpy(s_fota.url, url_item->valuestring, sizeof(s_fota.url) - 1);
    s_fota.url[sizeof(s_fota.url) - 1] = '\0';
    s_fota.crc32       = new_crc32;
    s_fota.retry_count = 0;
    s_fota.pending     = true;

    log_info(TAG, "FOTA update latched: crc32=0x%08lX url=%s", (unsigned long)new_crc32, s_fota.url);

    cJSON_Delete(root);
}

bool fota_is_pending(void)
{
    return s_fota.pending;
}



/* Internal flash layout, mirrored from BOOTLOADER_WS/bootloader/
 * flash_define.h - same "local copy, not shared header" precedent as
 * ota_trigger.c (see that file's header comment for why). Only the two
 * addresses this module needs are reproduced.
 *
 * Bug fix (2026-08-05): FOTA_SECONDARY_APP_ADDR moved from 0x08088000 to
 * 0x08178000 - the old address was inside FLASH BANK 1 on the STM32H563
 * (Bank 1 = 0x08000000-0x080FFFFF), the SAME bank the Primary app (and
 * this very function, fota_download_attempt(), which calls
 * sx_flash_erase()/sx_flash_write() on this address) executes from.
 * Confirmed on real hardware: this caused an immediate HardFault inside
 * fota_download_attempt() the moment the erase/program touched Bank 1
 * flash while the CPU was fetching code from that same bank. See
 * flash_define.h's matching comment on FACTORY_APP_FLASH_START_ADDRESS/
 * SECONDARY_APP_FLASH_START_ADDRESS for the full layout and the
 * deployment note about the on-flash partition table needing a reset
 * (erase FOTA_PARTITION_TABLE_ADDR, NOT Factory's sectors) before this
 * new address takes effect on an already-provisioned board. */
#define FOTA_SECONDARY_APP_ADDR    0x08100000UL
#define FOTA_SECONDARY_APP_SIZE    (60UL * 8192UL)  /* 480KB, 60 sectors - must match
                                                       * FOTA_MAX_FIRMWARE_SIZE (fota.h) */
#define FOTA_PARTITION_TABLE_ADDR  0x0800E000UL
#define FOTA_PARTITION_TABLE_SIZE  0x2000UL /* 8KB, one sector */

/* Struct layout, mirrored from BOOTLOADER_WS/bootloader/boot_flash.h's
 * BootFlashPartition_t - EXACT same copy as ota_trigger.c's
 * ota_boot_partition_t (see that file for the full field-order/type
 * warning - if that struct's layout ever changes on the bootloader side,
 * BOTH copies must be updated together). Kept as a second local copy
 * here rather than sharing ota_trigger.c's static type, matching this
 * project's existing precedent of not cross-including one user/module's
 * internals from another. */
typedef struct {
    uint32_t primary_app_address;
    uint32_t primary_app_number_of_sectors;
    uint32_t primary_app_size;
    uint32_t secondary_app_address;
    uint32_t secondary_app_number_of_sectors;
    uint32_t secondary_app_size;
    uint32_t factory_app_address;
    uint32_t factory_app_number_of_sectors;
    uint32_t factory_app_size;
    uint32_t scratch_start_address;
    uint32_t scratch_number_of_sectors;
    uint32_t scratch_size;
    bool     isNewFirmwareAvailable;
    bool     isUpgradeInProgress;
    uint32_t magic_number;
} fota_boot_partition_t;

/* One HTTP Range request size - see a7677s_http.h's file header comment
 * on the two-level chunking (this is the outer level; a7677s_http.c
 * handles the inner AT+HTTPREAD-sized reads on its own). Kept at
 * A7677S_HTTP_RANGE_SIZE (4096, the max this module allows per call) to
 * minimize the number of slow AT+HTTPACTION round-trips (up to 120s
 * MaxResponseTime each, per a76xx_at_cmd.md) across a real multi-hundred-
 * KB firmware image. */
#define FOTA_HTTP_CHUNK_SIZE   A7677S_HTTP_RANGE_SIZE

/* Software CRC32, IEEE 802.3 polynomial (0xEDB88320, reflected form) -
 * the same algorithm zlib's crc32(), Python's zlib.crc32(), and the
 * standard Linux `crc32` utility all use, so whatever tool the server
 * side uses to compute the "crc32" field in the retained FOTA_CHECK
 * message (see fota_on_message()) will match this by construction,
 * without needing to pin down which specific tool that is. No hardware
 * CRC peripheral is initialized anywhere in this codebase (confirmed by
 * grep - no hcrc/HAL_CRC/CRC_HandleTypeDef usage in board/ or app/), so
 * this is a plain table-driven software implementation rather than a
 * call into ST's HAL_CRC_*() API. Table is computed once on first use
 * (static local) rather than a 1KB compile-time literal, to keep this
 * file's own footprint small - not thread-safe by construction, but this
 * codebase is single-threaded/non-RTOS throughout (see modem.c's similar
 * assumptions elsewhere), so that is not a concern here. */
/* Shared table + one-byte lookup step, factored out of what used to be a
 * single self-contained fota_crc32(data,len) function, so that BOTH a
 * one-shot whole-buffer call (fota_crc32() below, kept for any future
 * caller that has the whole buffer in RAM at once) AND the incremental
 * flash-verify loop (fota_download_attempt(), further down - reads flash
 * back in FOTA_HTTP_CHUNK_SIZE pieces rather than one multi-hundred-KB
 * buffer) can reuse the exact same table without duplicating the
 * polynomial-generation loop in two places. table_ready/table are function-
 * static, not file-static globals, deliberately - this keeps the "is the
 * table built yet" state colocated with the one piece of code that
 * initializes it, same reasoning as before the split. */
static uint32_t fota_crc32_table_lookup(uint32_t crc, uint8_t byte)
{
    static uint32_t table[256];
    static bool      table_ready = false;

    if (!table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        table_ready = true;
    }

    return table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
}

/* One-shot whole-buffer CRC32, built on fota_crc32_table_lookup() above.
 * Not currently called anywhere in this file (the flash-verify loop in
 * fota_download_attempt() calls fota_crc32_table_lookup() directly, byte by
 * byte, since it never has the whole region in one buffer - see that
 * loop's own comment) - kept as a small public-shaped helper in case a
 * future caller (e.g. a unit test, or a caller that already holds the
 * whole image in RAM) needs a one-call CRC32 without re-deriving the
 * init/finalize XOR dance itself. */
static uint32_t fota_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = fota_crc32_table_lookup(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* --- Generic blocking-wait helper for this file's two async AT ops --- */

/* Shared "did it finish, what happened" capture for both
 * a7677s_http_ssl_configure() and a7677s_http_get_range() below - two
 * different callback shapes (modem_ops_result_t vs the richer HTTP range
 * result), so this holds both result types but each call site only reads
 * the one that applies to it. Kept as one struct (not two) so
 * fota_wait_done() below can be shared by both wait loops instead of
 * duplicating the "poll both functions every tick, time out after
 * FOTA_RANGE_WAIT_TIMEOUT_MS" loop body twice. */
typedef struct {
    bool                        done;
    modem_ops_result_t          ssl_result;
    a7677s_http_range_result_t  range_result;
    int                         status_code;
    uint32_t                    data_len;
    uint8_t                     data_copy[FOTA_HTTP_CHUNK_SIZE];
} fota_async_wait_t;

static void on_fota_ssl_configured(modem_ops_result_t result, void *ctx)
{
    fota_async_wait_t *w = (fota_async_wait_t *)ctx;
    w->ssl_result = result;
    w->done = true;
}

static void on_fota_range_done(a7677s_http_range_result_t result,
                                int status_code,
                                const uint8_t *data,
                                uint32_t data_len,
                                void *ctx)
{
    fota_async_wait_t *w = (fota_async_wait_t *)ctx;
    w->range_result = result;
    w->status_code  = status_code;
    w->data_len     = data_len;
    if (result == A7677S_HTTP_RANGE_OK && data_len > 0) {
        /* Copy out of a7677s_http.c's internal buffer NOW - per
         * a7677s_http.h's doc-comment on a7677s_http_range_cb_t, that
         * buffer is "only valid for the duration of this callback".
         * fota_download()'s loop reads w->data_copy after this callback
         * has already returned, so it cannot reference `data` directly.
         * data_len is always <= FOTA_HTTP_CHUNK_SIZE here because
         * fota_download() never requests a range_len larger than that -
         * see a7677s_http_get_range()'s own "range_len must be <=
         * A7677S_HTTP_RANGE_SIZE" contract, which FOTA_HTTP_CHUNK_SIZE is
         * defined equal to. */
        memcpy(w->data_copy, data, data_len);
    }
    w->done = true;
}

/* Blocking wait for w->done, driving both required poll functions every
 * tick meanwhile - same two-call pattern test_http.c's test_http_poll()
 * uses (modem_handle_poll() then a7677s_http_poll()), wrapped in a
 * blocking loop instead of being driven from the caller's own
 * non-blocking poll function, since fota_download() as a whole is
 * already a deliberate one-off blocking exception (see fota.h's
 * doc-comment on why). sx_delay_ms(1) paces the loop instead of spinning
 * as fast as possible, matching this codebase's other blocking wait
 * loops (see sx_sleep_manager.c's equivalent pattern for its own
 * blocking steps). delta_ms passed to the poll functions is always 1
 * (this loop's own pacing), NOT the real wall-clock delta since the
 * previous main-loop tick - acceptable because a7677s_http_poll()'s and
 * modem_poll()'s only use of their delta_ms argument is their own
 * internal timeout accumulation (raw_state_elapsed_ms / waitElapsed),
 * and this blocking loop provides its OWN outer timeout
 * (FOTA_RANGE_WAIT_TIMEOUT_MS below) that fires well before those
 * internal ones would ever need to matter here.
 * Returns false if no callback fired within elapsed_cap_ms - should not
 * happen in practice (a7677s_http.c has its own internal timeouts that
 * always eventually fire the callback), guarded against anyway rather
 * than looping forever against a genuinely stuck modem. */
#define FOTA_RANGE_WAIT_TIMEOUT_MS   30000UL

static bool fota_wait_done(fota_async_wait_t *w, uint32_t elapsed_cap_ms)
{
    uint32_t waited_ms = 0;
    while (!w->done) {
        modem_handle_poll(&board.modem, 1);
        a7677s_http_poll(&board.a7677s, 1);
        sx_delay_ms(1);
        waited_ms++;
        if (waited_ms >= elapsed_cap_ms) {
            log_error(TAG, "fota_wait_done(): timed out after %lu ms with no callback",
                       (unsigned long)elapsed_cap_ms);
            return false;
        }
    }
    return true;
}

/* Erases the whole Secondary partition once, up front, before the
 * download loop starts writing into it - NOT erased incrementally per
 * chunk. This is deliberate: FOTA_HTTP_CHUNK_SIZE (4096) is not a
 * multiple of FLASH_SECTOR_BYTES (8192, see sx_flash.c), so successive
 * 4KB chunks straddle sector boundaries in a pattern that would make
 * "erase only the sectors this chunk touches, without re-erasing a
 * sector a previous chunk already wrote real data into" fiddly and
 * error-prone to get right for a first implementation. Erasing the whole
 * partition once up front sidesteps that entirely, at the cost of a
 * slightly longer one-time erase before the first byte is written -
 * acceptable given fota_download() is already a rare, multi-second
 * blocking operation (see fota.h). */
static bool fota_erase_secondary(void)
{
    log_info(TAG, "Erasing Secondary partition (%lu bytes at 0x%08lX)...",
              (unsigned long)FOTA_SECONDARY_APP_SIZE, (unsigned long)FOTA_SECONDARY_APP_ADDR);
    sx_flash_unlock();
    sx_flash_erase(FOTA_SECONDARY_APP_ADDR, FOTA_SECONDARY_APP_SIZE);
    sx_flash_lock();
    return true;
}

/* Flips isNewFirmwareAvailable=true + isUpgradeInProgress=false together
 * (see fota.h's doc-comment on why BOTH, unlike ota_trigger.c's
 * ota_trigger_enter_dfu() which only ever sets isUpgradeInProgress) - this
 * mirrors ota_trigger_enter_dfu()'s exact read-modify-write-verify shape
 * (same magic-number check, same read-back verification before
 * resetting), since it is the same partition table, just flipping a
 * different flag combination. Does not return on success (calls
 * NVIC_SystemReset()). Returns false (does NOT reset) if the partition
 * table looks corrupt or the write cannot be verified - matches
 * ota_trigger_enter_dfu()'s "refuse rather than risk bricking" behavior
 * exactly. */
static bool fota_trigger_swap_and_reset(void)
{
    fota_boot_partition_t partition;

    sx_flash_read(FOTA_PARTITION_TABLE_ADDR, (uint8_t *)&partition, sizeof(partition));

    if (partition.magic_number != 0xDEADBEEFUL) {
        log_error(TAG, "Partition table magic mismatch (0x%08lX) - refusing to write, "
                        "bootloader state looks corrupt",
                   (unsigned long)partition.magic_number);
        return false;
    }

    partition.isUpgradeInProgress    = false;
    partition.isNewFirmwareAvailable = true;

    sx_flash_unlock();
    sx_flash_erase(FOTA_PARTITION_TABLE_ADDR, FOTA_PARTITION_TABLE_SIZE);
    sx_flash_write(FOTA_PARTITION_TABLE_ADDR, (const uint8_t *)&partition, sizeof(partition));
    sx_flash_lock();

    fota_boot_partition_t verify;
    sx_flash_read(FOTA_PARTITION_TABLE_ADDR, (uint8_t *)&verify, sizeof(verify));
    if (!verify.isNewFirmwareAvailable || verify.isUpgradeInProgress ||
        verify.magic_number != partition.magic_number) {
        log_error(TAG, "Partition table write verification failed - aborting reset");
        return false;
    }

    /* Persist s_fota.crc32 (the image that just passed CRC32 verification
     * and is about to be swapped in) as "last applied" BEFORE resetting -
     * see this file's Part 1 header comment for why this is needed at
     * all (a retained MQTT message does not disappear after being read,
     * so without this the device would re-download the same firmware
     * forever) and why a backup register rather than flash/RAM. Written
     * here, not inside fota_download() after this function returns,
     * because this function does not return on the success path
     * (NVIC_SystemReset() below) - this is the only point in the whole
     * success path guaranteed to run after verification passed but before
     * the reset actually happens. */
    fota_backup_set_last_applied_crc32(s_fota.crc32);

    log_info(TAG, "FOTA flags written (isNewFirmwareAvailable=1), resetting to apply update");
    HAL_Delay(100); /* let the log line actually go out before reset - same as ota_trigger.c */
    NVIC_SystemReset();

    return true; /* unreachable */
}

/* One attempt: erase, download+write every FOTA_HTTP_CHUNK_SIZE-sized
 * range in sequence, verify CRC32 of the whole written partition against
 * s_fota.crc32. Returns true only if every range downloaded successfully
 * AND the final CRC32 matched - fota_download() (below) is the only
 * caller, and treats any false return as "this attempt failed", handling
 * the retry-counter bookkeeping itself so this function stays a pure
 * download+verify primitive without needing to know about retry policy.
 *
 * Design change (2026-08-05, per the user): the retained FOTA_CHECK
 * payload no longer carries a "size" field - fota.h originally described
 * {"version","url","size","crc32"}, but the user deliberately dropped
 * BOTH "version" and "size" (see fota_on_message()'s doc-comment for why
 * "version" was dropped and replaced by crc32-as-identity). Without a
 * server-declared size, this function no longer knows in advance how many
 * bytes to expect, so it:
 *   - bounds the download loop by FOTA_SECONDARY_APP_SIZE (the hard
 *     ceiling - a fixed 480KB partition, same constant fota.h's
 *     FOTA_MAX_FIRMWARE_SIZE already documented as the ceiling even back
 *     when "size" existed) instead of s_fota.size, and
 *   - detects true end-of-file the same way a7677s_http.h's
 *     a7677s_http_range_cb_t doc-comment already told callers to handle
 *     regardless (data_len may be "shorter than the requested range size
 *     - e.g. last range in the file" - this was already a documented
 *     possibility, just previously treated as a sanity-check against a
 *     size the server told us; now it is the ONLY EOF signal available:
 *     w.data_len < this_len means the server has no more bytes to give,
 *     stop the loop there, that offset is the real end of the file). */
static bool fota_download_attempt(void)
{
    if (!fota_erase_secondary()) {
        return false;
    }

    uint32_t offset = 0;
    bool     eof_reached = false;
    while (offset < FOTA_SECONDARY_APP_SIZE && !eof_reached) {
        uint32_t remaining = FOTA_SECONDARY_APP_SIZE - offset;
        uint32_t this_len  = (remaining < FOTA_HTTP_CHUNK_SIZE) ? remaining : FOTA_HTTP_CHUNK_SIZE;

        /* Bug fix (2026-08-05): same stack-overflow fix as ssl_wait above
         * (see that comment for the full explanation) - static removes
         * this ~4.1KB struct from the stack. Explicitly memset every loop
         * iteration since a static variable does NOT reset itself between
         * iterations the way a stack local with "= {0}" would have. */
        static fota_async_wait_t w;
        memset(&w, 0, sizeof(w));
        int ret = a7677s_http_get_range(&board.a7677s, s_fota.url, offset, this_len,
                                          on_fota_range_done, &w);
        if (ret != 0) {
            log_error(TAG, "a7677s_http_get_range() rejected immediately at offset %lu "
                            "(modem busy) - aborting this attempt", (unsigned long)offset);
            return false;
        }

        if (!fota_wait_done(&w, FOTA_RANGE_WAIT_TIMEOUT_MS)) {
            return false; /* fota_wait_done() already logged the timeout */
        }

        if (w.range_result != A7677S_HTTP_RANGE_OK) {
            log_error(TAG, "Range at offset %lu failed (result=%d status=%d) - aborting this attempt",
                       (unsigned long)offset, (int)w.range_result, w.status_code);
            return false;
        }

        if (w.data_len == 0) {
            /* Zero bytes on the VERY FIRST range (offset==0) means the URL
             * itself is bad/unreachable/empty - a genuinely-empty firmware
             * file is not a real scenario worth accepting silently. Zero
             * bytes on a LATER range, immediately after a previous range
             * that itself came back short (already caught by the
             * data_len < this_len check below, which sets eof_reached and
             * exits the loop before a next call is ever made) should not
             * normally happen - if it does, treat it the same as any other
             * failed range rather than silently accepting a 0-byte
             * "final" chunk, since eof_reached should have already stopped
             * the loop one iteration earlier in the legitimate case. */
            log_error(TAG, "Range at offset %lu returned 0 bytes (expected up to %lu) - "
                            "aborting this attempt, server/URL may not support Range requests correctly",
                       (unsigned long)offset, (unsigned long)this_len);
            return false;
        }

        /* Bug-avoidance note: sx_flash_write() (sx_flash.c, STM32H5 path)
         * programs in fixed 16-byte quadwords and pads any leftover tail
         * with 0xFF when len is not a multiple of 16 - safe ONLY on the
         * very last write to a given region, since padding mid-stream
         * would silently corrupt real firmware bytes that a later write
         * was supposed to supply. Every chunk here except possibly the
         * final one is exactly FOTA_HTTP_CHUNK_SIZE (4096, a multiple of
         * 16), so this only matters for the last chunk's w.data_len -
         * which is fine precisely BECAUSE it is the last write to this
         * region: whatever 0xFF padding sx_flash_write() adds past
         * w.data_len bytes lands in space this download was never going
         * to fill anyway (the file legitimately ends there), not into
         * bytes a subsequent chunk still needs to write. */
        sx_flash_unlock();
        sx_flash_write(FOTA_SECONDARY_APP_ADDR + offset, w.data_copy, w.data_len);
        sx_flash_lock();

        offset += w.data_len;

        log_info(TAG, "FOTA chunk OK: offset=%lu len=%lu (%lu bytes total so far)",
                  (unsigned long)(offset - w.data_len), (unsigned long)w.data_len,
                  (unsigned long)offset);

        /* True EOF signal, now that there is no server-declared size to
         * compare against (see this function's doc-comment above): a
         * range returning fewer bytes than requested means the server has
         * no more data to give from this offset onward - this is the
         * legitimate, expected way every successful download ends (unless
         * the file is an exact multiple of FOTA_HTTP_CHUNK_SIZE, in which
         * case the loop instead ends by reaching FOTA_SECONDARY_APP_SIZE,
         * or - far more likely for a real firmware image well under 480KB
         * - a future range would simply return 0 bytes, which is caught
         * above as an error rather than silently accepted; a real HTTP
         * server that supports Range requests correctly always returns
         * data_len == this_len for every non-final range, so this
         * comparison is not fooled by a slow/chunked transfer, only by
         * genuine end-of-file). */
        if (w.data_len < this_len) {
            eof_reached = true;
        }
    }

    if (offset == 0) {
        log_error(TAG, "Download loop ended with 0 bytes written - aborting");
        return false;
    }
    if (!eof_reached && offset >= FOTA_SECONDARY_APP_SIZE) {
        log_error(TAG, "Download reached the %lu-byte Secondary partition ceiling "
                        "without the server signaling EOF - firmware image may be "
                        "larger than the partition, or Range requests are not being "
                        "honored - aborting rather than risk a truncated image",
                   (unsigned long)FOTA_SECONDARY_APP_SIZE);
        return false;
    }

    uint32_t total_size = offset; /* real, server-determined size of this image */
    log_info(TAG, "Download complete: %lu bytes total", (unsigned long)total_size);

    /* Verify: re-read the whole written region back out of flash (not
     * the HTTP data as it arrived - reading back what actually landed in
     * flash catches a write that silently failed or a torn quadword, not
     * just a download-layer problem) and CRC32 it against the
     * server-provided checksum. Reads FOTA_HTTP_CHUNK_SIZE at a time
     * rather than the whole total_size in one sx_flash_read() call, to
     * avoid needing a second multi-hundred-KB RAM buffer alongside
     * fota_async_wait_t's own data_copy[] (this MCU's RAM budget is not
     * assumed to have room for two such buffers live at once - no
     * evidence either way was found in this codebase, so the more
     * conservative assumption is used here). Only reads/CRCs total_size
     * bytes (the actual downloaded length, now that there is no
     * server-declared size field to have pre-validated this against) -
     * NOT the full FOTA_SECONDARY_APP_SIZE partition, which would checksum
     * whatever stale/erased 0xFF filler bytes happen to sit past the real
     * image and never match s_fota.crc32. */
    uint32_t running_crc = 0xFFFFFFFFUL;
    uint32_t verify_offset = 0;
    while (verify_offset < total_size) {
        uint32_t remaining = total_size - verify_offset;
        uint32_t this_len  = (remaining < FOTA_HTTP_CHUNK_SIZE) ? remaining : FOTA_HTTP_CHUNK_SIZE;
        /* Bug fix (2026-08-05): same stack-overflow fix as ssl_wait/w above
         * - static instead of a 4KB stack-local array. No memset needed:
         * sx_flash_read() below fully overwrites buf[0..this_len) before
         * the CRC loop reads any byte of it. */
        static uint8_t buf[FOTA_HTTP_CHUNK_SIZE];

        sx_flash_read(FOTA_SECONDARY_APP_ADDR + verify_offset, buf, this_len);
        for (uint32_t i = 0; i < this_len; i++) {
            running_crc = fota_crc32_table_lookup(running_crc, buf[i]);
        }
        verify_offset += this_len;
    }
    uint32_t final_crc = running_crc ^ 0xFFFFFFFFUL;

    if (final_crc != s_fota.crc32) {
        log_error(TAG, "CRC32 mismatch after download: got 0x%08lX, expected 0x%08lX",
                   (unsigned long)final_crc, (unsigned long)s_fota.crc32);
        return false;
    }

    log_info(TAG, "CRC32 verified OK: 0x%08lX", (unsigned long)final_crc);
    return true;
}

void fota_download(void)
{
    if (!s_fota.pending) {
        log_warn(TAG, "fota_download() called with nothing pending - ignoring");
        return;
    }

    log_info(TAG, "Starting FOTA download attempt %lu/%lu: crc32=0x%08lX url=%s",
              (unsigned long)s_fota.retry_count + 1, (unsigned long)FOTA_MAX_RETRY_COUNT,
              (unsigned long)s_fota.crc32, s_fota.url);

    /* SSL context must be configured once before any https:// range call -
     * see a7677s_http_ssl_configure()'s doc-comment. Cheap/fast AT-layer
     * config; called every attempt rather than tracked as "already done
     * this boot" - idempotent on the modem side (just re-sets the same
     * authmode/SNI values), simpler than adding one-shot state for a rare
     * operation. Only needed for https:// URLs, per that function's
     * doc-comment - this project's URLs are expected to always be https
     * (GitHub raw content, matching test_http.c's precedent), but the
     * check is still made explicit rather than assumed. */
    if (strncmp(s_fota.url, "https://", 8) == 0) {
        /* Bug fix (2026-08-05): HardFault confirmed on real hardware right
         * at fota_download_attempt()'s entry (before its own first log
         * line even ran) - a stack-frame-creation fault, not a logic bug.
         * Root cause: fota_async_wait_t embeds a 4096-byte data_copy[]
         * (see that struct's doc-comment), so each stack-local instance
         * costs ~4.1KB. This ssl_wait instance stays alive for the WHOLE
         * duration of fota_download() (C does not free a local's stack
         * slot just because the code is logically done with it), which
         * includes the nested, blocking fota_download_attempt() call
         * below - which itself stack-allocates a SECOND fota_async_wait_t
         * (w) plus a THIRD 4KB buffer (buf, in the verify loop) - on top
         * of everything main()/test_fota_poll()/the MQTT+modem+HTTP
         * layers already used getting here. Static removes this instance
         * from the stack entirely. Safe here specifically because this is
         * a bare-metal, single main-loop build (no RTOS, no recursion,
         * confirmed by reading Core/Src/main.c and test_fota.c) and
         * fota_download() is a deliberate one-off blocking call - never
         * re-entered while already running, never called from more than
         * one calling context. */
        static fota_async_wait_t ssl_wait;
        memset(&ssl_wait, 0, sizeof(ssl_wait));
        int ret = a7677s_http_ssl_configure(&board.a7677s, on_fota_ssl_configured, &ssl_wait);
        if (ret != 0) {
            log_error(TAG, "a7677s_http_ssl_configure() rejected immediately - modem busy, aborting this attempt");
            return;
        }
        if (!fota_wait_done(&ssl_wait, FOTA_RANGE_WAIT_TIMEOUT_MS)) {
            return; /* fota_wait_done() already logged the timeout */
        }
        if (ssl_wait.ssl_result != MODEM_OPS_OK) {
            log_error(TAG, "a7677s_http_ssl_configure() failed - aborting this attempt, "
                            "HTTPS range calls would fail their TLS handshake");
            return;
        }
    }

    if (fota_download_attempt()) {
        /* Success - flip flags and reset. fota_trigger_swap_and_reset()
         * does not return on success. If it DOES return (partition table
         * verification failed), fall through to the failure path below -
         * treating a swap failure the same as a download failure keeps
         * the retry/give-up bookkeeping in one place rather than adding
         * a second distinct failure mode the caller of fota_download()
         * would need to understand. */
        if (fota_trigger_swap_and_reset()) {
            return; /* unreachable - kept for clarity/symmetry */
        }
    }

    /* Reached only on failure (download or verify or swap) - see
     * fota.h's doc-comment on FOTA_MAX_RETRY_COUNT. */
    s_fota.retry_count++;
    if (s_fota.retry_count >= FOTA_MAX_RETRY_COUNT) {
        log_error(TAG, "FOTA download failed %lu times for crc32=0x%08lX - giving up until a new "
                        "retained message is published", (unsigned long)s_fota.retry_count,
                   (unsigned long)s_fota.crc32);
        s_fota.pending = false;
    } else {
        log_warn(TAG, "FOTA download attempt %lu/%lu failed for crc32=0x%08lX - will retry next cycle",
                  (unsigned long)s_fota.retry_count, (unsigned long)FOTA_MAX_RETRY_COUNT,
                  (unsigned long)s_fota.crc32);
    }
}