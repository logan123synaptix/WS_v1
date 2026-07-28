#include "test_exflash.h"
#include "sx_board.h"
#include "sx_W25Q128.h"
#include "logger.h"
#include <string.h>

static const char *TAG = "TEST_EXFLASH";

/* Test region chosen deliberately far from any real data used elsewhere
 * (offline queue via LittleFS, filesystem metadata, etc.) — this test
 * writes/erases raw bytes at this address directly through the
 * sx_W25Q128_* driver API, bypassing any filesystem layer entirely, so
 * it must not land inside a sector that LittleFS/sx_ex_storage may
 * already be using. W25Q128 is 16MB total (W25Q128_TOTAL_BYTES); this
 * sector sits near the very end of the chip, away from low-address
 * space a filesystem would claim first. */
#define TEST_ADDR   (W25Q128_TOTAL_BYTES - W25Q128_SECTOR_SIZE)
#define TEST_LEN    64U

static uint8_t s_write_buf[TEST_LEN];
static uint8_t s_read_buf[TEST_LEN];
static uint32_t s_log_accum_ms = 0;
static bool s_test_passed = false;

/* board.q128 is already sx_W25Q128_init()'d inside sx_board_init()
 * (sx_board.c) — same reasoning as test_sht3x.c: do NOT re-init here,
 * that would just mask whether board init itself already succeeded. */
void test_exflash_init(void)
{
    log_info(TAG, "=== TEST W25Q128 (SPI ext-flash, addr=0x%06lX len=%u) ===",
              (unsigned long)TEST_ADDR, TEST_LEN);

    if (!board.q128.initialized) {
        log_error(TAG, "board.q128 not initialized — check SPI wiring/board init order");
        return;
    }

    /* Fill a recognizable pattern rather than all-0x00/0xFF, so a
     * bit-stuck-at fault or address-line fault is visible instead of
     * silently matching. */
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        s_write_buf[i] = (uint8_t)(0xA5 ^ i);
    }

    /* Erase first — W25Q128 (like all NOR flash) can only clear bits
     * 1->0 on program; a sector erase is required to guarantee the
     * target region is all-0xFF before writing, otherwise a stale
     * previous test's data could mask a program failure. */
    int erase_ret = sx_W25Q128_erase_sector(TEST_ADDR);
    if (erase_ret != 0) {
        log_error(TAG, "erase_sector FAILED (ret=%d)", erase_ret);
        return;
    }

    /* Busy-wait for the erase to complete before writing — this is a
     * one-shot bring-up test, not the main app loop, so blocking here
     * (rather than a non-blocking poll state machine) is acceptable and
     * keeps the test simple. sx_W25Q128_write()/erase_sector() already
     * poll the status register internally per the driver's own
     * implementation, so this is a belt-and-suspenders safety check,
     * not strictly required — kept for clarity of intent. */
    while (sx_W25Q128_is_busy()) {
        /* spin */
    }

    int write_ret = sx_W25Q128_write(TEST_ADDR, s_write_buf, TEST_LEN);
    if (write_ret != 0) {
        log_error(TAG, "write FAILED (ret=%d)", write_ret);
        return;
    }

    while (sx_W25Q128_is_busy()) {
        /* spin */
    }

    int read_ret = sx_W25Q128_read(TEST_ADDR, s_read_buf, TEST_LEN);
    if (read_ret != 0) {
        log_error(TAG, "read FAILED (ret=%d)", read_ret);
        return;
    }

    if (memcmp(s_write_buf, s_read_buf, TEST_LEN) == 0) {
        log_info(TAG, "Write/erase/read cycle OK — %u bytes verified", TEST_LEN);
        s_test_passed = true;
    } else {
        log_error(TAG, "Data mismatch — SPI link or flash chip is suspect");
    }
}

/* No periodic activity needed for a flash bring-up check (unlike a
 * sensor with an ongoing sampling cadence) — this just re-confirms the
 * one-shot init result on a slow cadence so the pass/fail status is
 * visible in the log stream without re-touching the flash. */
void test_exflash_poll(uint32_t delta_ms)
{
    s_log_accum_ms += delta_ms;
    if (s_log_accum_ms >= 5000U) {
        s_log_accum_ms = 0;
        log_info(TAG, "status: %s", s_test_passed ? "PASS" : "NOT PASSED");
    }
}