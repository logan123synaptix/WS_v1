#include "time_sync.h"
#include "logger.h"
#include <string.h>
#include <time.h>

static const char *TAG = "TIME_SYNC";

/* RX8130CE week register is a bitmask (RX8130CE_WEEK_SUN..SAT), not the
 * 0-6 struct tm.tm_wday ordinal — see sx_ex_rtc.h. struct tm.tm_wday is
 * 0=Sunday..6=Saturday, which lines up 1:1 with the bit position order
 * RX8130CE_WEEK_SUN..RX8130CE_WEEK_SAT are defined in (1<<0..1<<6). */
static uint8_t tm_wday_to_rx8130_week(int tm_wday)
{
    if (tm_wday < 0 || tm_wday > 6) {
        return RX8130CE_WEEK_SUN; /* defensive default, should not happen for a valid struct tm */
    }
    return (uint8_t)(1U << tm_wday);
}

/* struct tm -> rx8130ce_time_t. Caller must pass a struct tm already
 * normalized (tm_wday/tm_yday filled in, e.g. via mktime()+gmtime() as
 * a7677s.c's cb_cclk() already does for the modem path) since this
 * function does not call mktime() itself — the GPS path below computes
 * tm_wday explicitly for the same reason. */
static void tm_to_rx8130(const struct tm *t, rx8130ce_time_t *out)
{
    out->sec   = (uint8_t)t->tm_sec;
    out->min   = (uint8_t)t->tm_min;
    out->hour  = (uint8_t)t->tm_hour;
    out->week  = tm_wday_to_rx8130_week(t->tm_wday);
    out->day   = (uint8_t)t->tm_mday;
    out->month = (uint8_t)(t->tm_mon + 1);
    /* tm_year is "years since 1900"; RX8130CE only stores 2 digits
     * (0-99, see rx8130ce_set_time()'s range check) and this project is
     * nowhere near year 2100, so tm_year - 100 (i.e. tm_year+1900-2000)
     * is always in range for any real-world synced time. */
    out->year  = (uint8_t)(t->tm_year - 100);
}

void time_sync_init(time_sync_t *ts, modem_handle_t *modem, sx_gps_t *gps, rx8130ce_t *rtc)
{
    memset(ts, 0, sizeof(*ts));
    ts->modem = modem;
    ts->gps   = gps;
    ts->rtc   = rtc;
    ts->done  = 0;
}

/* Tries the modem (primary source) first; falls back to GPS only if the
 * modem hasn't synced anything yet. Sets ts->done on the first successful
 * RTC write from either source and never checks again after that — per
 * the module doc-comment in time_sync.h. */
void time_sync_poll(time_sync_t *ts)
{
    if (ts->done) {
        return;
    }

    struct tm t = {0};
    const char *source = NULL;

    if (ts->modem->ops->get_time_synced(ts->modem->ctx)) {
        if (ts->modem->ops->get_synced_time(ts->modem->ctx, &t)) {
            /* mktime()/gmtime() already ran inside a7677s.c's cb_cclk(),
             * so t.tm_wday is already correctly normalized here. */
            source = "modem (NITZ)";
        }
    }

    if (source == NULL) {
        /* GPS fallback — gps->tim is only ever populated by gps_process()
         * when an RMC sentence reports rmc.valid AND both time/date fields
         * are present (see gps.c's gps_callback_task()); before the first
         * such sentence, tim is all-zero (tm_year == 0, i.e. year 1900),
         * which is what tm_year > 0 below is checking for — a real fix's
         * year is always well past 1900. gps->tim.tm_wday is NOT computed
         * by minmea_getdatetime() (NMEA RMC does not carry a weekday
         * field), so it must be derived here via mktime()/gmtime() before
         * writing to the RTC, same as the modem path already gets for
         * free from a7677s.c. */
        if (ts->gps->tim.tm_year > 0) {
            struct tm gps_tm = ts->gps->tim;
            gps_tm.tm_isdst = 0;
            time_t as_time = mktime(&gps_tm);
            if (as_time != (time_t)-1) {
                struct tm *normalized = gmtime(&as_time);
                if (normalized) {
                    t = *normalized;
                    source = "GPS";
                }
            }
        }
    }

    if (source == NULL) {
        return; /* neither source has a valid time yet — try again next tick */
    }

    rx8130ce_time_t rtc_time;
    tm_to_rx8130(&t, &rtc_time);

    int err = rx8130ce_set_time(ts->rtc, &rtc_time);
    if (err == RX8130CE_OK) {
        log_info(TAG, "RTC synced from %s: %04d-%02d-%02d %02d:%02d:%02d UTC",
                 source, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
        ts->done = 1;
    } else {
        log_warn(TAG, "rx8130ce_set_time() failed (err=%d), will retry next tick", err);
        /* ts->done stays 0 — retried automatically on the next poll. */
    }
}

uint8_t time_sync_is_done(const time_sync_t *ts)
{
    return ts->done;
}