#include "test_rtc.h"
#include "sx_board.h"
#include "sx_ex_rtc.h"
#include "logger.h"

static const char *TAG = "TEST_RTC";

/* Set to 1 to write a known fixed time once at boot before the periodic
 * read-back below — useful the very first time you test a fresh/blank
 * chip (VLF will read '1' = invalid on power-up until a time is
 * written at least once, per rx8130ce_init()'s VLF-check comment in
 * sx_ex_rtc.c). Leave at 0 once you've confirmed the RTC already keeps
 * time across reset/reflash (VBAT-backed), so you don't stomp on a
 * clock that's already running correctly. */
#define TEST_RTC_SET_TIME_ON_INIT 1

/* Arbitrary known value, easy to eyeball against wall-clock time while
 * watching the log — adjust to "now" before flashing if you want to
 * sanity-check against a real clock rather than just watching it count
 * up correctly. */
static const rx8130ce_time_t s_seed_time = {
    .sec   = 0,
    .min   = 0,
    .hour  = 12,
    .week  = RX8130CE_WEEK_WED,
    .day   = 29,
    .month = 7,
    .year  = 26, /* 2026 */
};

static const char *weekday_str(uint8_t week_mask)
{
    switch (week_mask) {
        case RX8130CE_WEEK_SUN: return "Sun";
        case RX8130CE_WEEK_MON: return "Mon";
        case RX8130CE_WEEK_TUE: return "Tue";
        case RX8130CE_WEEK_WED: return "Wed";
        case RX8130CE_WEEK_THU: return "Thu";
        case RX8130CE_WEEK_FRI: return "Fri";
        case RX8130CE_WEEK_SAT: return "Sat";
        default:                return "???"; /* multiple/no bits set — unexpected, still print raw mask below */
    }
}

/* Poll cadence for read-back — RTC only ticks in whole seconds, so
 * anything faster than 1000ms just re-reads the same second. */
#define TEST_RTC_SAMPLE_PERIOD_MS 1000U

static uint32_t s_accum_ms = 0;

void test_rtc_init(void)
{
    log_info(TAG, "=== TEST RTC RX8130CE (I2C1, addr=0x%02X) ===",
              RX8130CE_ADDR >> 1);

    /* board.rtc was already rx8130ce_init()'d inside sx_board_init()
     * (sx_board.c) — do NOT call rx8130ce_init() again here, it would
     * re-run the VLF check / possible soft-reset sequence for no
     * reason and mask whether board init itself already handled it. */

    bool valid = false;
    int ret = rx8130ce_is_time_valid(&board.rtc, &valid);
    if (ret != RX8130CE_OK) {
        log_error(TAG, "is_time_valid() FAILED (err=%d) — check I2C1 wiring/address", ret);
    } else {
        log_info(TAG, "VLF check at boot: time %s (VLF=%d)",
                  valid ? "VALID" : "INVALID (oscillation was stopped / never set)",
                  valid ? 0 : 1);
    }

#if TEST_RTC_SET_TIME_ON_INIT
    log_info(TAG, "Seeding time to 2026-07-29 12:00:00 (Wed)...");
    int set_ret = rx8130ce_set_time(&board.rtc, &s_seed_time);
    if (set_ret != RX8130CE_OK) {
        log_error(TAG, "rx8130ce_set_time() FAILED (err=%d)", set_ret);
    } else {
        log_info(TAG, "Seed OK — now watching it count up every %u ms",
                  (unsigned)TEST_RTC_SAMPLE_PERIOD_MS);
    }
#endif
}

void test_rtc_poll(uint32_t delta_ms)
{
    s_accum_ms += delta_ms;
    if (s_accum_ms < TEST_RTC_SAMPLE_PERIOD_MS) {
        return;
    }
    s_accum_ms = 0;

    rx8130ce_time_t t;
    int ret = rx8130ce_get_time(&board.rtc, &t);
    if (ret != RX8130CE_OK) {
        log_error(TAG, "get_time() FAILED (err=%d)", ret);
        return;
    }

    log_info(TAG, "%04d-%02d-%02d %02d:%02d:%02d %s",
              2000 + t.year, t.month, t.day,
              t.hour, t.min, t.sec,
              weekday_str(t.week));
}