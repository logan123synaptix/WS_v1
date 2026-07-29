#include "test_exflash.h"
#include "sx_ex_storage.h"
#include "logger.h"
#include <string.h>

static const char *TAG = "TEST_EXFLASH";

/* Deliberately not under "/queue/..." — that prefix is app.c's real
 * offline MQTT queue directory (see app.c's telemetry resend logic);
 * using a different top-level filename here avoids any chance of this
 * test colliding with or leaving stale junk in that queue. */
#define TEST_EXFLASH_PATH "/test_exflash.bin"

static const char s_write_payload[] = "SynaptiX WS_v1 W25Q128 test payload 0123456789";

/* One-shot state machine, run from poll() so it doesn't block the main
 * loop with SPI/filesystem transfers, but only actually needs a
 * handful of ticks (no waiting on external hardware timing, unlike
 * GPS/SHT3x/RTC) — step order chosen to be non-destructive to any real
 * data first, and to end by cleaning up after itself so re-flashing/
 * re-running this test repeatedly does not leave a stale file behind. */
typedef enum {
    TEST_EXFLASH_STEP_INFO = 0,
    TEST_EXFLASH_STEP_WRITE,
    TEST_EXFLASH_STEP_EXISTS,
    TEST_EXFLASH_STEP_SIZE,
    TEST_EXFLASH_STEP_READ_VERIFY,
    TEST_EXFLASH_STEP_DELETE,
    TEST_EXFLASH_STEP_DELETE_VERIFY,
    TEST_EXFLASH_STEP_DONE,
} test_exflash_step_t;

static test_exflash_step_t s_step = TEST_EXFLASH_STEP_INFO;

void test_exflash_init(void)
{
    log_info(TAG, "=== TEST W25Q128 (via sx_storage_* API, SPI ext-flash) ===");

    /* sx_storage_init() was already called inside sx_board_init()
     * (sx_board.c, board.storage_cfg) — do NOT call it again here. If
     * it had failed, every sx_storage_*() call below will consistently
     * return SX_STORAGE_ERR_NOT_INIT, which is itself the diagnostic
     * (check the SX_STORAGE log tag output at boot, before this test's
     * own logs, for the real root cause — sx_ex_storage.c logs
     * "Storage init OK" or "W25Q128 init failed"/"Filesystem init
     * failed" there). */

    s_step = TEST_EXFLASH_STEP_INFO;
}

void test_exflash_poll(uint32_t delta_ms)
{
    (void)delta_ms; /* no timing dependency — each step is a single blocking SPI/FS call */

    switch (s_step) {

    case TEST_EXFLASH_STEP_INFO: {
        int32_t total = sx_storage_total_space();
        int32_t free  = sx_storage_free_space();
        if (total < 0 || free < 0) {
            log_error(TAG, "free/total_space() FAILED — storage not initialized, "
                            "check SX_STORAGE tag log at boot for the real cause");
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        log_info(TAG, "Filesystem: total=%ld bytes, free=%ld bytes", (long)total, (long)free);
        s_step = TEST_EXFLASH_STEP_WRITE;
        break;
    }

    case TEST_EXFLASH_STEP_WRITE: {
        sx_storage_err_t ret = sx_storage_write(TEST_EXFLASH_PATH,
                                                 s_write_payload,
                                                 sizeof(s_write_payload));
        if (ret != SX_STORAGE_OK) {
            log_error(TAG, "sx_storage_write() FAILED (err=%d)", ret);
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        log_info(TAG, "Write OK: %s (%u bytes)", TEST_EXFLASH_PATH, (unsigned)sizeof(s_write_payload));
        s_step = TEST_EXFLASH_STEP_EXISTS;
        break;
    }

    case TEST_EXFLASH_STEP_EXISTS: {
        bool exists = sx_storage_exists(TEST_EXFLASH_PATH);
        if (!exists) {
            log_error(TAG, "sx_storage_exists() returned false right after a successful write");
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        log_info(TAG, "Exists check OK");
        s_step = TEST_EXFLASH_STEP_SIZE;
        break;
    }

    case TEST_EXFLASH_STEP_SIZE: {
        int32_t sz = sx_storage_size(TEST_EXFLASH_PATH);
        if (sz != (int32_t)sizeof(s_write_payload)) {
            log_error(TAG, "sx_storage_size() = %ld, expected %u",
                       (long)sz, (unsigned)sizeof(s_write_payload));
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        log_info(TAG, "Size check OK: %ld bytes", (long)sz);
        s_step = TEST_EXFLASH_STEP_READ_VERIFY;
        break;
    }

    case TEST_EXFLASH_STEP_READ_VERIFY: {
        char readback[sizeof(s_write_payload)] = {0};
        sx_storage_err_t ret = sx_storage_read(TEST_EXFLASH_PATH, readback, sizeof(readback));
        if (ret != SX_STORAGE_OK) {
            log_error(TAG, "sx_storage_read() FAILED (err=%d)", ret);
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        if (memcmp(readback, s_write_payload, sizeof(s_write_payload)) != 0) {
            log_error(TAG, "Read-back MISMATCH — got: \"%s\"", readback);
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        log_info(TAG, "Read-back OK, content matches: \"%s\"", readback);
        s_step = TEST_EXFLASH_STEP_DELETE;
        break;
    }

    case TEST_EXFLASH_STEP_DELETE: {
        sx_storage_err_t ret = sx_storage_delete(TEST_EXFLASH_PATH);
        if (ret != SX_STORAGE_OK) {
            log_error(TAG, "sx_storage_delete() FAILED (err=%d) — test file left behind at %s",
                       ret, TEST_EXFLASH_PATH);
            s_step = TEST_EXFLASH_STEP_DONE;
            break;
        }
        log_info(TAG, "Delete OK");
        s_step = TEST_EXFLASH_STEP_DELETE_VERIFY;
        break;
    }

    case TEST_EXFLASH_STEP_DELETE_VERIFY: {
        bool exists = sx_storage_exists(TEST_EXFLASH_PATH);
        if (exists) {
            log_error(TAG, "File still exists after delete()");
        } else {
            log_info(TAG, "Delete-verify OK — file gone");
        }
        log_info(TAG, "=== W25Q128 TEST PASS ===");
        s_step = TEST_EXFLASH_STEP_DONE;
        break;
    }

    case TEST_EXFLASH_STEP_DONE:
    default:
        /* Nothing more to do — this test is one-shot, not periodic
         * like GPS/SHT3x/RTC (there is no "keep watching a sensor"
         * angle for a flash read/write/delete round-trip). */
        break;
    }
}