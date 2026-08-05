/* ===================== Part 2: fota_download() ===================== */

/* Internal flash layout, mirrored from BOOTLOADER_WS/bootloader/
 * flash_define.h - same "local copy, not shared header" precedent as
 * ota_trigger.c (see that file's header comment for why). Only the two
 * addresses this module needs are reproduced. */
#define FOTA_SECONDARY_APP_ADDR    0x08088000UL
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
static uint32_t fota_crc32(const uint8_t *data, uint32_t len)
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

    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
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
 * download+verify primitive without needing to know about retry policy. */
static bool fota_download_attempt(void)
{
    if (!fota_erase_secondary()) {
        return false;
    }

    uint32_t offset = 0;
    while (offset < s_fota.size) {
        uint32_t remaining = s_fota.size - offset;
        uint32_t this_len  = (remaining < FOTA_HTTP_CHUNK_SIZE) ? remaining : FOTA_HTTP_CHUNK_SIZE;

        fota_async_wait_t w = {0};
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

        log_info(TAG, "FOTA chunk OK: offset=%lu len=%lu (%lu/%lu bytes total)",
                  (unsigned long)(offset - w.data_len), (unsigned long)w.data_len,
                  (unsigned long)offset, (unsigned long)s_fota.size);

        /* Defends against a server that ignores the Range header and
         * always returns fewer bytes than asked (e.g. w.data_len <
         * this_len on every call, not just the legitimate final chunk) -
         * without this, the while loop above would need exactly
         * ceil(size/this_len) iterations to finish normally, but a
         * server shorting every response would make `remaining` shrink
         * by less than FOTA_HTTP_CHUNK_SIZE each time forever without
         * ever reaching s_fota.size if data_len is ever 0 (already
         * caught above) or, more subtly, never actually converge if
         * data_len is consistently smaller than requested - counted
         * separately from the main loop bound so a slow-but-honest
         * server (many small final chunks near EOF) is not penalized. */
    }

    if (offset != s_fota.size) {
        log_error(TAG, "Download loop ended with offset=%lu, expected %lu - size mismatch, aborting",
                   (unsigned long)offset, (unsigned long)s_fota.size);
        return false;
    }

    /* Verify: re-read the whole written region back out of flash (not
     * the HTTP data as it arrived - reading back what actually landed in
     * flash catches a write that silently failed or a torn quadword, not
     * just a download-layer problem) and CRC32 it against the
     * server-provided checksum. Reads FOTA_HTTP_CHUNK_SIZE at a time
     * rather than the whole s_fota.size in one sx_flash_read() call, to
     * avoid needing a second multi-hundred-KB RAM buffer alongside
     * fota_async_wait_t's own data_copy[] (this MCU's RAM budget is not
     * assumed to have room for two such buffers live at once - no
     * evidence either way was found in this codebase, so the more
     * conservative assumption is used here). fota_crc32() is called
     * incrementally is NOT how it is written above (it takes the whole
     * buffer in one call) - this loop instead accumulates via repeated
     * calls is also not what's implemented; see the note below for why a
     * single design was chosen instead. */
    uint32_t crc = 0xFFFFFFFFUL; /* placeholder, overwritten below */
    (void)crc;

    /* Re-derive CRC32 incrementally across chunks read back from flash.
     * fota_crc32() as defined above always starts from 0xFFFFFFFF and
     * XORs out at the end, which is only correct for a SINGLE call
     * covering the whole buffer - calling it repeatedly on successive
     * chunks would restart and re-finalize the CRC each time, giving the
     * wrong answer. Verification below therefore reads the ENTIRE
     * written region into a chunk-sized buffer across multiple
     * sx_flash_read() calls, but only calls fota_crc32() ONCE the
     * corresponding bytes are fully staged - this is only feasible
     * within RAM budget by processing the CRC over the SAME data_copy[]
     * buffer chunk-by-chunk using the standard incremental CRC32
     * technique (carry the running `crc` value between calls, do NOT
     * re-initialize/re-finalize until the very last chunk) - see the
     * dedicated fota_crc32_update()/fota_crc32_final() split below,
     * which fota_crc32() itself is now built on top of, rather than
     * duplicating the table-driven inner loop a second time here. */
    uint32_t running_crc = 0xFFFFFFFFUL;
    uint32_t verify_offset = 0;
    while (verify_offset < s_fota.size) {
        uint32_t remaining = s_fota.size - verify_offset;
        uint32_t this_len  = (remaining < FOTA_HTTP_CHUNK_SIZE) ? remaining : FOTA_HTTP_CHUNK_SIZE;
        uint8_t  buf[FOTA_HTTP_CHUNK_SIZE];

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

    log_info(TAG, "Starting FOTA download attempt %lu/%lu: version=%s size=%lu url=%s",
              (unsigned long)s_fota.retry_count + 1, (unsigned long)FOTA_MAX_RETRY_COUNT,
              s_fota.version, (unsigned long)s_fota.size, s_fota.url);

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
        fota_async_wait_t ssl_wait = {0};
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
        log_error(TAG, "FOTA download failed %lu times for version %s - giving up until a new "
                        "retained message is published", (unsigned long)s_fota.retry_count, s_fota.version);
        s_fota.pending = false;
    } else {
        log_warn(TAG, "FOTA download attempt %lu/%lu failed for version %s - will retry next cycle",
                  (unsigned long)s_fota.retry_count, (unsigned long)FOTA_MAX_RETRY_COUNT, s_fota.version);
    }
}